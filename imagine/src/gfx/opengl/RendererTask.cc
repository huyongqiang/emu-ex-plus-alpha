/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "RendererTask"
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/gfx/DrawableHolder.hh>
#include <imagine/gfx/RendererTask.hh>
#include <imagine/base/Base.hh>
#include <imagine/base/Screen.hh>
#include <imagine/base/Window.hh>
#include <imagine/thread/Thread.hh>
#include "private.hh"

namespace Gfx
{

void GLRendererTask::initVBOs()
{
	#ifndef CONFIG_GFX_OPENGL_ES
	if(likely(streamVBO[0]))
		return;
	logMsg("making stream VBO");
	glGenBuffers(streamVBO.size(), streamVBO.data());
	#endif
}

GLuint GLRendererTask::getVBO()
{
	#ifndef CONFIG_GFX_OPENGL_ES
	assert(streamVBO[streamVBOIdx]);
	auto vbo = streamVBO[streamVBOIdx];
	streamVBOIdx = (streamVBOIdx+1) % streamVBO.size();
	return vbo;
	#else
	return 0;
	#endif
}

void GLRendererTask::initVAO()
{
	#ifndef CONFIG_GFX_OPENGL_ES
	if(likely(streamVAO))
		return;
	logMsg("making stream VAO");
	glGenVertexArrays(1, &streamVAO);
	glBindVertexArray(streamVAO);
	#endif
}

void GLRendererTask::initDefaultFramebuffer()
{
	#ifdef CONFIG_GLDRAWABLE_NEEDS_FRAMEBUFFER
	if(!defaultFB)
	{
		Base::GLContext::setCurrent(Base::GLDisplay::getDefault(), glContext(), {});
		glGenFramebuffers(1, &defaultFB);
		logMsg("created default framebuffer:%u", defaultFB);
		glBindFramebuffer(GL_FRAMEBUFFER, defaultFB);
	}
	#endif
}

GLuint GLRendererTask::bindFramebuffer(Texture &tex)
{
	assert(tex);
	if(unlikely(!fbo))
	{
		glGenFramebuffers(1, &fbo);
		logMsg("init FBO:0x%X", fbo);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.texName(), 0);
	return fbo;
}

void GLRendererTask::setRenderer(Renderer *r_)
{
	r = r_;
}

Renderer &RendererTask::renderer() const
{
	return *r;
}

GLRendererTask::GLRendererTask(const char *debugLabel, Renderer &r, Base::GLContext context):
	GLMainTask{debugLabel, context, true}, r{&r}
{}

RendererTask::RendererTask(RendererTask &&o)
{
	*this = std::move(o);
}

RendererTask &RendererTask::operator=(RendererTask &&o)
{
	GLRendererTask::operator=(std::move(o));
	o.r = {};
	return *this;
}

void GLRendererTask::doPreDraw(DrawableHolder &drawableHolder, Base::Window &win, Base::WindowDrawParams winParams, DrawParams &params)
{
	if(unlikely(!context))
	{
		logWarn("draw() called without context");
		return;
	}
	if(winParams.wasResized())
	{
		if(win == Base::mainWindow())
		{
			if(!Config::SYSTEM_ROTATES_WINDOWS)
			{
				r->setProjectionMatrixRotation(orientationToGC(win.softOrientation()));
				Base::setOnDeviceOrientationChanged(
					[&renderer = *r, &win](Base::Orientation newO)
					{
						auto oldWinO = win.softOrientation();
						if(win.requestOrientationChange(newO))
						{
							renderer.animateProjectionMatrixRotation(win, orientationToGC(oldWinO), orientationToGC(newO));
						}
					});
			}
			else if(Config::SYSTEM_ROTATES_WINDOWS && !Base::Window::systemAnimatesRotation())
			{
				Base::setOnSystemOrientationChanged(
					[&renderer = *r, &win](Base::Orientation oldO, Base::Orientation newO) // TODO: parameters need proper type definitions in API
					{
						const Angle orientationDiffTable[4][4]
						{
							{0, angleFromDegree(90), angleFromDegree(-180), angleFromDegree(-90)},
							{angleFromDegree(-90), 0, angleFromDegree(90), angleFromDegree(-180)},
							{angleFromDegree(-180), angleFromDegree(-90), 0, angleFromDegree(90)},
							{angleFromDegree(90), angleFromDegree(-180), angleFromDegree(-90), 0},
						};
						auto rotAngle = orientationDiffTable[oldO][newO];
						logMsg("animating from %d degrees", (int)angleToDegree(rotAngle));
						renderer.animateProjectionMatrixRotation(win, rotAngle, 0.);
					});
			}
		}
	}
	if(unlikely(!drawableHolder))
	{
		drawableHolder.makeDrawable(*r, *static_cast<RendererTask*>(this), win);
	}
	if(unlikely(winParams.needsSync()))
	{
		params.setAsyncMode(DrawAsyncMode::NONE);
	}
}

RendererTask::operator bool() const
{
	return GLMainTask::operator bool();
}

void RendererTask::updateDrawableForSurfaceChange(DrawableHolder &drawableHolder, Base::Window &win, Base::WindowSurfaceChange change)
{
	if(change.destroyed())
	{
		destroyDrawable(drawableHolder);
	}
	else if(!drawableHolder)
	{
		drawableHolder.makeDrawable(renderer(), *this, win);
	}
	if(change.reset())
	{
		resetDrawable = true;
	}
}

void RendererTask::destroyDrawable(DrawableHolder &drawableHolder)
{
	awaitPending();
	drawableHolder.destroyDrawable(renderer());
}

bool GLRendererTask::handleDrawableReset()
{
	if(resetDrawable)
	{
		resetDrawable = false;
		return true;
	}
	return false;
}

void GLRendererTask::initialCommands(RendererCommands &cmds)
{
	if(likely(contextInitialStateSet))
		return;
	if(cmds.renderer().support.hasVBOFuncs)
		initVBOs();
	#ifndef CONFIG_GFX_OPENGL_ES
	if(cmds.renderer().useStreamVAO)
		initVAO();
	#endif
	runGLCheckedVerbose([&]()
	{
		glEnableVertexAttribArray(VATTR_POS);
	}, "glEnableVertexAttribArray(VATTR_POS)");
	cmds.setClearColor(0, 0, 0);
	contextInitialStateSet = true;
}

void GLRendererTask::verifyCurrentContext(Base::GLDisplay glDpy) const
{
	if(!Config::DEBUG_BUILD)
		return;
	auto currentCtx = Base::GLContext::current(glDpy);
	if(unlikely(glContext() != currentCtx))
	{
		bug_unreachable("expected GL context:%p but current is:%p", glContext().nativeObject(), currentCtx.nativeObject());
	}
}

SyncFence RendererTask::addSyncFence()
{
	if(!r->support.hasSyncFences())
		return {}; // no-op
	GLsync sync;
	runSync(
		[&support = r->support, &sync](TaskContext ctx)
		{
			sync = support.fenceSync(ctx.glDisplay());
		});
	return sync;
}

void RendererTask::deleteSyncFence(SyncFence fence)
{
	if(!fence.sync)
		return;
	assumeExpr(r->support.hasSyncFences());
	const bool canPerformInCurrentThread = Config::Base::GL_PLATFORM_EGL;
	if(canPerformInCurrentThread)
	{
		auto dpy = renderer().glDpy;
		renderer().support.deleteSync(dpy, fence.sync);
	}
	else
	{
		run(
			[&support = r->support, sync = fence.sync](TaskContext ctx)
			{
				support.deleteSync(ctx.glDisplay(), sync);
			});
	}
}

void RendererTask::clientWaitSync(SyncFence fence, int flags, std::chrono::nanoseconds timeout)
{
	if(!fence.sync)
		return;
	assumeExpr(r->support.hasSyncFences());
	const bool canPerformInCurrentThread = Config::Base::GL_PLATFORM_EGL && !flags;
	if(canPerformInCurrentThread)
	{
		//logDMsg("waiting on sync:%p flush:%s timeout:0%llX", fence.sync, flags & 1 ? "yes" : "no", (unsigned long long)timeout);
		auto dpy = renderer().glDpy;
		renderer().support.clientWaitSync(dpy, fence.sync, 0, timeout.count());
		renderer().support.deleteSync(dpy, fence.sync);
	}
	else
	{
		runSync(
			[&support = r->support, sync = fence.sync, timeout, flags](TaskContext ctx)
			{
				support.clientWaitSync(ctx.glDisplay(), sync, flags, timeout.count());
				ctx.notifySemaphore();
				support.deleteSync(ctx.glDisplay(), sync);
			});
	}
}

SyncFence RendererTask::clientWaitSyncReset(SyncFence fence, int flags, std::chrono::nanoseconds timeout)
{
	clientWaitSync(fence, flags, timeout);
	return addSyncFence();
}

void RendererTask::waitSync(SyncFence fence)
{
	if(!fence.sync)
		return;
	assumeExpr(r->support.hasSyncFences());
	run(
		[&support = r->support, sync = fence.sync](TaskContext ctx)
		{
			support.waitSync(ctx.glDisplay(), sync);
			support.deleteSync(ctx.glDisplay(), sync);
		});
}

void RendererTask::awaitPending()
{
	if(!*this)
		return;
	runSync([](){});
}

void RendererTask::flush()
{
	run(
		[]()
		{
			glFlush();
		});
}

void RendererTask::releaseShaderCompiler()
{
	#ifdef CONFIG_GFX_OPENGL_SHADER_PIPELINE
	run(
		[]()
		{
			glReleaseShaderCompiler();
		});
	#endif
}

GLRendererTaskDrawContext::GLRendererTaskDrawContext(GLRendererTask &task, GLMainTask::TaskContext taskCtx, bool notifySemaphoreAfterPresent):
	task{static_cast<RendererTask*>(&task)}, drawCompleteSemPtr{taskCtx.semaphorePtr()}, glDpy{taskCtx.glDisplay()}, notifySemaphoreAfterPresent{notifySemaphoreAfterPresent}
{}

RendererCommands RendererTaskDrawContext::makeRendererCommands(DrawableHolder &drawableHolder, Base::Window &win, Viewport viewport, Mat4 projMat)
{
	task->initDefaultFramebuffer();
	RendererCommands cmds{*task, &win, drawableHolder, glDpy, drawCompleteSemPtr, notifySemaphoreAfterPresent};
	task->initialCommands(cmds);
	cmds.setViewport(viewport);
	cmds.setProjectionMatrix(projMat);
	return cmds;
}

RendererTask &RendererTaskDrawContext::rendererTask() const
{
	return *task;
}

Renderer &RendererTaskDrawContext::renderer() const
{
	return rendererTask().renderer();
}

}
