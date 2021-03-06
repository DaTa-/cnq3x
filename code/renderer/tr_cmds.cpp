/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"

volatile qbool	renderThreadActive;


void R_InitCommandBuffers()
{
	glInfo.smpActive = qfalse;
#ifdef USE_R_SMP
	if ( !r_smp->integer )
		return;

	ri.Printf( PRINT_ALL, "Trying SMP acceleration...\n" );
	if ( GLimp_SpawnRenderThread( RB_RenderThread ) ) {
		ri.Printf( PRINT_ALL, "...succeeded.\n" );
		glInfo.smpActive = qtrue;
	} else {
		ri.Printf( PRINT_ALL, "...failed.\n" );
	}
#endif	
}


void R_ShutdownCommandBuffers()
{
	// kill the rendering thread
#ifdef USE_R_SMP
	if ( glInfo.smpActive ) {
		GLimp_WakeRenderer( NULL );
	}
#endif	
    glInfo.smpActive = qfalse;
}


static void R_IssueRenderCommands()
{
	static int c_blockedOnRender, c_blockedOnMain;

	renderCommandList_t* cmdList = &backEndData[tr.smpFrame]->commands;

	// add an end-of-list command
	*(int *)(cmdList->cmds + cmdList->used) = RC_END_OF_LIST;

	// clear it out, in case this is a sync and not a buffer flip
	cmdList->used = 0;

#ifdef USE_R_SMP
	if ( glInfo.smpActive ) {
		// if the render thread is not idle, wait for it
		if ( renderThreadActive ) {
			c_blockedOnRender++;
			if ( r_showSmp->integer ) {
				ri.Printf( PRINT_ALL, "R" );
			}
		} else {
			c_blockedOnMain++;
			if ( r_showSmp->integer ) {
				ri.Printf( PRINT_ALL, "." );
			}
		}
		// sleep until the renderer has completed
		GLimp_FrontEndSleep();
	}

	// actually start the commands going
	if ( !r_skipBackEnd->integer ) {
		// let it start on the new batch
		if ( !glInfo.smpActive ) {
			RB_ExecuteRenderCommands( cmdList->cmds );
		} else {
			GLimp_WakeRenderer( cmdList );
		}
	}
#else
    RB_ExecuteRenderCommands( cmdList->cmds );
#endif
}


/*
====================
R_SyncRenderThread

Issue any pending commands and wait for them to complete.
After exiting, the render thread will have completed its work
and will remain idle and the main thread is free to issue
OpenGL calls until R_IssueRenderCommands is called.
====================
*/
void R_SyncRenderThread( void ) {
	if ( !tr.registered ) {
		return;
	}
	R_IssueRenderCommands();

#ifdef USE_R_SMP
	if ( !glInfo.smpActive ) {
		return;
	}
	GLimp_FrontEndSleep();
#endif	
}

/*
============
R_GetCommandBuffer

make sure there is enough command space, waiting on the
render thread if needed.
============
*/
void *R_GetCommandBuffer( int bytes ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData[tr.smpFrame]->commands;

	// always leave room for the end of list command
	if ( cmdList->used + bytes + 4 > MAX_RENDER_COMMANDS ) {
		if ( bytes > MAX_RENDER_COMMANDS - 4 ) {
			ri.Error( ERR_FATAL, "R_GetCommandBuffer: bad size %i", bytes );
		}
		// if we run out of room, just start dropping commands
		return NULL;
	}

	cmdList->used += bytes;

	return cmdList->cmds + cmdList->used - bytes;
}


// technically, all commands should probably check tr.registered
// but realistically, only begin+end frame really need to
#define R_CMD(T, ID) T* cmd = (T*)R_GetCommandBuffer( sizeof(T) ); if (!cmd) return; cmd->commandId = ID;


void R_AddDrawSurfCmd( drawSurf_t* drawSurfs, int numDrawSurfs )
{
	R_CMD( drawSurfsCommand_t, RC_DRAW_SURFS );

	cmd->drawSurfs = drawSurfs;
	cmd->numDrawSurfs = numDrawSurfs;

	cmd->refdef = tr.refdef;
	cmd->viewParms = tr.viewParms;
}


// passing NULL will set the color to white

void RE_SetColor( const float* rgba )
{
	R_CMD( setColorCommand_t, RC_SET_COLOR );

	if ( !rgba )
		rgba = colorWhite;

	cmd->color[0] = rgba[0];
	cmd->color[1] = rgba[1];
	cmd->color[2] = rgba[2];
	cmd->color[3] = rgba[3];
}


void RE_StretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader )
{
	R_CMD( stretchPicCommand_t, RC_STRETCH_PIC );

	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}


// if running in stereo, RE_BeginFrame will be called twice for each RE_EndFrame

void RE_BeginFrame( stereoFrame_t stereoFrame )
{
	if (!tr.registered)
		return;

	glState.finishCalled = qfalse;

	tr.frameCount++;
	tr.frameSceneNum = 0;

	//
	// do overdraw measurement
	//
	if ( r_measureOverdraw->integer )
	{
		if ( glConfig.stencilBits < 4 )
		{
			ri.Printf( PRINT_ALL, "Warning: not enough stencil bits to measure overdraw: %d\n", glConfig.stencilBits );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else
		{
			R_SyncRenderThread();
			qglEnable( GL_STENCIL_TEST );
			qglStencilMask( ~0U );
			qglClearStencil( 0U );
			qglStencilFunc( GL_ALWAYS, 0U, ~0U );
			qglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
		}
		r_measureOverdraw->modified = qfalse;
	}
	else
	{
		// this is only reached if it was on and is now off
		if ( r_measureOverdraw->modified ) {
			R_SyncRenderThread();
			qglDisable( GL_STENCIL_TEST );
		}
		r_measureOverdraw->modified = qfalse;
	}

	//
	// texturemode stuff
	//
	if ( r_textureMode->modified ) {
		R_SyncRenderThread();
		GL_TextureMode( r_textureMode->string );
		r_textureMode->modified = qfalse;
	}

	//
	// gamma stuff
	//
	if ( r_gamma->modified ) {
		r_gamma->modified = qfalse;
		R_SyncRenderThread();
		R_SetColorMappings();
	}

	// check for errors
	if ( !r_ignoreGLErrors->integer ) {
		int err;
		R_SyncRenderThread();
		if ( ( err = qglGetError() ) != GL_NO_ERROR ) {
			ri.Error( ERR_FATAL, "RE_BeginFrame() - glGetError() failed (0x%x)!\n", err );
		}
	}

	//
	// draw buffer stuff
	//
	R_CMD( drawBufferCommand_t, RC_DRAW_BUFFER );

	if ( glConfig.stereoEnabled ) {
		if ( stereoFrame == STEREO_LEFT ) {
			cmd->buffer = (int)GL_BACK_LEFT;
		} else if ( stereoFrame == STEREO_RIGHT ) {
			cmd->buffer = (int)GL_BACK_RIGHT;
		} else {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is enabled, but stereoFrame was %i", stereoFrame );
		}
	} else {
		if ( stereoFrame != STEREO_CENTER ) {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is disabled, but stereoFrame was %i", stereoFrame );
		}
		if ( !Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) ) {
			cmd->buffer = (int)GL_FRONT;
		} else {
			cmd->buffer = (int)GL_BACK;
		}
	}
}


void RE_EndFrame( int* pcFE, int* pc2D, int* pc3D )
{
	if (!tr.registered)
		return;

	R_CMD( swapBuffersCommand_t, RC_SWAP_BUFFERS );

	R_IssueRenderCommands();

	// use the other buffers next frame, because another CPU
	// may still be rendering into the current ones
	R_ToggleSmpFrame();

	if (pcFE)
		Com_Memcpy( pcFE, &tr.pc, sizeof( tr.pc ) );

	if (pc2D)
		Com_Memcpy( pc2D, &backEnd.pc2D, sizeof( backEnd.pc2D ) );

	if (pc3D)
		Com_Memcpy( pc3D, &backEnd.pc3D, sizeof( backEnd.pc3D ) );

	Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
	Com_Memset( &backEnd.pc2D, 0, sizeof( backEnd.pc2D ) );
	Com_Memset( &backEnd.pc3D, 0, sizeof( backEnd.pc3D ) );
}


void RE_TakeVideoFrame( int width, int height, byte *captureBuffer, byte *encodeBuffer, qbool motionJpeg )
{
	R_CMD( videoFrameCommand_t, RC_VIDEOFRAME );

	cmd->width = width;
	cmd->height = height;
	cmd->captureBuffer = captureBuffer;
	cmd->encodeBuffer = encodeBuffer;
	cmd->motionJpeg = motionJpeg;
}
