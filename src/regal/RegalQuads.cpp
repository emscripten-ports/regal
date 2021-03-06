/*
  Copyright (c) 2011-2013 NVIDIA Corporation
  Copyright (c) 2011-2012 Cass Everitt
  Copyright (c) 2012 Scott Nations
  Copyright (c) 2012 Mathias Schott
  Copyright (c) 2012 Nigel Stewart
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
  OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*

 Regal GL_QUADs emulation layer
 Scott Nations

 Limitations:

  - If either front or back rendering modes is FILL then triangles will
    be drawn.  Otherwise if either mode is LINE then lines will be drawn.
    Else points will be drawn.

  - Some attempt is made to respect glCullFace when culling is enabled,
    but lines and points are likely to be rendered when they otherwise
    would if rendering quads using those glPolygonModes.

  - The colors of lines and points are probably going to be wrong when
    using flat shading, i.e. glShadeModel(GL_FLAT).

*/


/*

From glspec44.compatibility.withchanges.pdf, page 485

Primitive type of polygon i	First vertex convention	Last vertex convention

independent quad		4i - 3 			4i		(*1)
				4i			4i 		(*2)
quad strip 			2i - 1 			2i + 2		(*1)
				2i + 2 			2i + 2		(*2)

Table 13.2: Provoking vertex selection. The vertex colors and/or output values
used for flatshading the ith primitive generated by drawing commands with the
indicated primitive type are derived from the corresponding values of the vertex
whose index is shown in the table. Vertices are numbered 1 through n, where n is
the number of vertices drawn.

*1 If the value of QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION is TRUE.
*2 If the value of QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION is FALSE.

*/

#include "RegalUtil.h"

#if REGAL_EMULATION

#include "RegalQuads.h"
#include "RegalContextInfo.h"
#include "RegalToken.h"

REGAL_GLOBAL_BEGIN

#include <GL/Regal.h>

#include <vector>

REGAL_GLOBAL_END

REGAL_NAMESPACE_BEGIN

namespace Emu
{

void Quads::Init(RegalContext &ctx)
{
  elementArrayBuffer = 0;
  DispatchTableGL & d = *ctx.dispatcher.emulation.next();
  d.call(&d.glGenBuffers)(1, &quadIndexBuffer);
  windingMode = GL_CCW;
  frontFaceMode = backFaceMode = GL_FILL;
  shadeMode = GL_SMOOTH;
  provokeMode = GL_LAST_VERTEX_CONVENTION;
  cullFace = GL_BACK;
  gl_quads_follow_provoking_vertex_convention = (ctx.info->gl_quads_follow_provoking_vertex_convention == GL_TRUE);
  cullingFaces = false;
}

void Quads::Cleanup(RegalContext &ctx)
{
  UNUSED_PARAMETER(ctx);
}

bool Quads::glDrawArrays(RegalContext *ctx, GLenum mode, GLint first, GLsizei count)
{
  RegalAssert(ctx);

  if (mode != GL_QUADS && mode != GL_QUAD_STRIP)
    return false;

  Internal("Regal::Emu::Quads::glDrawArrays(", Token::toString(mode), ", ", first, ", ", count, ")");

  // count < 0 should generate an error

  if ( count < 0 )
      return false;

  // if we don't have at least 4 then we're done

  if ( count < 4 )
      return true;

  // draw nothing if we're culling all faces

  if (cullingFaces && cullFace == GL_FRONT_AND_BACK)
    return true;

  // else draw a surface, lines, or points.  The driver will cull
  // surfaces, but we need to choose whether to send lines of points

  bool drawQuads  = false;
  bool drawLines  = false;
  bool drawPoints = false;

  if (!cullingFaces)
  {
    drawQuads  = (frontFaceMode == GL_FILL  || backFaceMode == GL_FILL);
    drawLines  = (frontFaceMode == GL_LINE  || backFaceMode == GL_LINE);
    drawPoints = (frontFaceMode == GL_POINT || backFaceMode == GL_POINT);
  }
  else
  {
    if (cullFace == GL_BACK)
    {
      drawQuads  = (frontFaceMode == GL_FILL );
      drawLines  = (frontFaceMode == GL_LINE );
      drawPoints = (frontFaceMode == GL_POINT);
    }
    else if (cullFace == GL_FRONT)
    {
      drawQuads  = (backFaceMode == GL_FILL );
      drawLines  = (backFaceMode == GL_LINE);
      drawPoints = (backFaceMode == GL_POINT);
    }
    else
      return true;
  }

  Internal("Regal::Emu::Quads: shadeMode     =", Token::toString(shadeMode));
  Internal("Regal::Emu::Quads: windingMode   =", Token::toString(windingMode));
  Internal("Regal::Emu::Quads: provokeMode   =", Token::toString(provokeMode));
  Internal("Regal::Emu::Quads: convention    =", gl_quads_follow_provoking_vertex_convention);
  Internal("Regal::Emu::Quads: frontFaceMode =", Token::toString(frontFaceMode));
  Internal("Regal::Emu::Quads: backFaceMode  =", Token::toString(backFaceMode));
  Internal("Regal::Emu::Quads: cullingFaces  =", cullingFaces);
  Internal("Regal::Emu::Quads: cullFace      =", Token::toString(cullFace));
  Internal("Regal::Emu::Quads: drawQuads     =", drawQuads);
  Internal("Regal::Emu::Quads: drawLines     =", drawLines);
  Internal("Regal::Emu::Quads: drawPoints    =", drawPoints);

  DispatchTableGL &dt = ctx->dispatcher.emulation;

#define EMU_QUADS_BUFFER_SIZE 1024

  if (drawQuads)
  {
    if (frontFaceMode != GL_FILL || backFaceMode != GL_FILL)
      dt.call(&dt.glPolygonMode)(GL_FRONT_AND_BACK, GL_FILL);

    GLsizei myCount = count &= (( mode == GL_QUADS ) ? (~0x3) : (~0x1));

    if ( mode == GL_QUAD_STRIP )
    {
      // convert quad strips into triangles

      GLuint  indices[EMU_QUADS_BUFFER_SIZE * 6];
      GLsizei n = ((myCount/2-1) < EMU_QUADS_BUFFER_SIZE) ? (myCount/2-1) : EMU_QUADS_BUFFER_SIZE;
      if (shadeMode != GL_FLAT)
      {
        for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
        {
#if 1
          // split 1
          indices[ii * 6 + 0] = first + ii * 2 + 1;
          indices[ii * 6 + 1] = first + ii * 2 + 3;
          indices[ii * 6 + 2] = first + ii * 2 + 0;
          indices[ii * 6 + 3] = first + ii * 2 + 2;
          indices[ii * 6 + 4] = first + ii * 2 + 0;
          indices[ii * 6 + 5] = first + ii * 2 + 3;
#else
          // split 1
          indices[ii * 6 + 0] = first + ii * 2 + 0;
          indices[ii * 6 + 1] = first + ii * 2 + 1;
          indices[ii * 6 + 2] = first + ii * 2 + 2;
          indices[ii * 6 + 3] = first + ii * 2 + 3;
          indices[ii * 6 + 4] = first + ii * 2 + 2;
          indices[ii * 6 + 5] = first + ii * 2 + 1;

          // split 3
          indices[ii * 6 + 0] = first + ii * 2 + 1;
          indices[ii * 6 + 1] = first + ii * 2 + 0;
          indices[ii * 6 + 2] = first + ii * 2 + 3;
          indices[ii * 6 + 3] = first + ii * 2 + 2;
          indices[ii * 6 + 4] = first + ii * 2 + 3;
          indices[ii * 6 + 5] = first + ii * 2 + 0;

          // split 4
          indices[ii * 6 + 0] = first + ii * 2 + 0;
          indices[ii * 6 + 1] = first + ii * 2 + 2;
          indices[ii * 6 + 2] = first + ii * 2 + 1;
          indices[ii * 6 + 3] = first + ii * 2 + 3;
          indices[ii * 6 + 4] = first + ii * 2 + 1;
          indices[ii * 6 + 5] = first + ii * 2 + 2;
#endif
        }
      }
      else
      {
        if (!gl_quads_follow_provoking_vertex_convention || (provokeMode == GL_LAST_VERTEX_CONVENTION))
        {
          for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
          {
            indices[ii * 6 + 0] = first + ii * 2 + 0;
            indices[ii * 6 + 1] = first + ii * 2 + 1;
            indices[ii * 6 + 2] = first + ii * 2 + 3;
            indices[ii * 6 + 3] = first + ii * 2 + 2;
            indices[ii * 6 + 4] = first + ii * 2 + 0;
            indices[ii * 6 + 5] = first + ii * 2 + 3;
          }
        }
        else
        {
          for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
          {
            indices[ii * 6 + 0] = first + ii * 2 + 1;
            indices[ii * 6 + 1] = first + ii * 2 + 3;
            indices[ii * 6 + 2] = first + ii * 2 + 0;
            indices[ii * 6 + 3] = first + ii * 2 + 3;
            indices[ii * 6 + 4] = first + ii * 2 + 2;
            indices[ii * 6 + 5] = first + ii * 2 + 0;
          }
        }
      }

      dt.call(&dt.glBindBuffer)( GL_ELEMENT_ARRAY_BUFFER, quadIndexBuffer);
      while (myCount >= 2)
      {
        dt.call(&dt.glBufferData)( GL_ELEMENT_ARRAY_BUFFER, n * 6 * sizeof( GLuint ), indices, GL_STATIC_DRAW );
        Internal("Regal::Emu::Quads::glDrawArrays","glDrawElements(GL_TRIANGLES,",n*6,",GL_UNSIGNED_INT, [])");
        dt.call(&dt.glDrawElements)(GL_TRIANGLES, n*6, GL_UNSIGNED_INT, 0);
        myCount -= (EMU_QUADS_BUFFER_SIZE * 2);

        if (myCount >= 2)
        {
          n = ((myCount/2-1) < EMU_QUADS_BUFFER_SIZE) ? (myCount/2-1) : EMU_QUADS_BUFFER_SIZE;
          for (GLsizei ii=0; ii<(n * 6); ii++)
            indices[ii] += (EMU_QUADS_BUFFER_SIZE * 2);
        }
      }
      dt.call(&dt.glBindBuffer)( GL_ELEMENT_ARRAY_BUFFER, elementArrayBuffer );
    }
    else if ( mode == GL_QUADS )
    {
      // convert quads into triangles

      GLuint  indices[EMU_QUADS_BUFFER_SIZE * 6];
      GLsizei n = ((myCount/4) < EMU_QUADS_BUFFER_SIZE) ? (myCount/4) : EMU_QUADS_BUFFER_SIZE;
      if (shadeMode != GL_FLAT)
      {
        for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
        {
          indices[ii * 6 + 0] = first + ii * 4 + 0;
          indices[ii * 6 + 1] = first + ii * 4 + 1;
          indices[ii * 6 + 2] = first + ii * 4 + 2;
          indices[ii * 6 + 3] = first + ii * 4 + 3;
          indices[ii * 6 + 4] = first + ii * 4 + 0;
          indices[ii * 6 + 5] = first + ii * 4 + 2;
        }
      }
      else
      {
        if (!gl_quads_follow_provoking_vertex_convention || (provokeMode == GL_LAST_VERTEX_CONVENTION))
        {
          for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
          {
            indices[ii * 6 + 0] = first + ii * 4 + 0;
            indices[ii * 6 + 1] = first + ii * 4 + 1;
            indices[ii * 6 + 2] = first + ii * 4 + 3;
            indices[ii * 6 + 3] = first + ii * 4 + 1;
            indices[ii * 6 + 4] = first + ii * 4 + 2;
            indices[ii * 6 + 5] = first + ii * 4 + 3;
          }
        }
        else
        {
          for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
          {
            indices[ii * 6 + 0] = first + ii * 4 + 1;
            indices[ii * 6 + 1] = first + ii * 4 + 2;
            indices[ii * 6 + 2] = first + ii * 4 + 0;
            indices[ii * 6 + 3] = first + ii * 4 + 2;
            indices[ii * 6 + 4] = first + ii * 4 + 3;
            indices[ii * 6 + 5] = first + ii * 4 + 0;
          }
        }
      }


      dt.call(&dt.glBindBuffer)( GL_ELEMENT_ARRAY_BUFFER, quadIndexBuffer);
      while (myCount >= 4)
      {
        dt.call(&dt.glBufferData)( GL_ELEMENT_ARRAY_BUFFER, n * 6 * sizeof( GLuint ), indices, GL_STATIC_DRAW );
        Internal("Regal::Emu::Quads::glDrawArrays","glDrawElements(GL_TRIANGLES,",n*6,",GL_UNSIGNED_INT, [])");
        dt.call(&dt.glDrawElements)(GL_TRIANGLES, n*6, GL_UNSIGNED_INT, 0);
        myCount -= (EMU_QUADS_BUFFER_SIZE * 4);

        if (myCount >= 4)
        {
          n = ((myCount/4) < EMU_QUADS_BUFFER_SIZE) ? (myCount/4) : EMU_QUADS_BUFFER_SIZE;
          for (GLsizei ii=0; ii<(n * 6); ii++)
            indices[ii] += (EMU_QUADS_BUFFER_SIZE * 4);
        }
      }
      dt.call(&dt.glBindBuffer)( GL_ELEMENT_ARRAY_BUFFER, elementArrayBuffer );

    }

    if (frontFaceMode != GL_FILL)
      dt.call(&dt.glPolygonMode)(GL_FRONT, frontFaceMode);
    if (backFaceMode != GL_FILL)
      dt.call(&dt.glPolygonMode)(GL_BACK, backFaceMode);
  }
  else if (drawLines)
  {
    if (frontFaceMode != GL_LINE || backFaceMode != GL_LINE)
      dt.call(&dt.glPolygonMode)(GL_FRONT_AND_BACK, GL_LINE);

    GLsizei myCount = count &= (( mode == GL_QUADS ) ? (~0x3) : (~0x1));

    if ( mode == GL_QUAD_STRIP )
    {
      // convert quad strips into quad outlines

      GLuint  indices[EMU_QUADS_BUFFER_SIZE * 6 + 2];
      GLsizei n = ((myCount/2-1) < EMU_QUADS_BUFFER_SIZE) ? (myCount/2-1) : EMU_QUADS_BUFFER_SIZE;

      if (shadeMode == GL_FLAT && gl_quads_follow_provoking_vertex_convention && (provokeMode == GL_FIRST_VERTEX_CONVENTION))
      {
        for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
        {
          indices[ii * 6 + 0] = first + ii * 2 + 3;
          indices[ii * 6 + 1] = first + ii * 2 + 1;
          indices[ii * 6 + 2] = first + ii * 2 + 1;
          indices[ii * 6 + 3] = first + ii * 2 + 0;
          indices[ii * 6 + 4] = first + ii * 2 + 2;
          indices[ii * 6 + 5] = first + ii * 2 + 0;
        }
        indices[n * 6 + 0] = first + n * 2 + 1;
        indices[n * 6 + 1] = first + n * 2 + 0;
      }
      else
      {
        indices[0] = first + 0;
        indices[1] = first + 1;
        for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
        {
          indices[ii * 6 + 2] = first + ii * 2 + 0;
          indices[ii * 6 + 3] = first + ii * 2 + 2;
          indices[ii * 6 + 4] = first + ii * 2 + 2;
          indices[ii * 6 + 5] = first + ii * 2 + 3;
          indices[ii * 6 + 6] = first + ii * 2 + 1;
          indices[ii * 6 + 7] = first + ii * 2 + 3;
        }
      }

      while (myCount >= 2)
      {
        Internal("Regal::Emu::Quads::glDrawArrays","glDrawElements(GL_LINES,",n*6+2,",GL_UNSIGNED_INT, [])");
        dt.call(&dt.glDrawElements)(GL_LINES, n*6+2, GL_UNSIGNED_INT, indices);
        myCount -= (EMU_QUADS_BUFFER_SIZE * 2);

        if (myCount >= 2)
        {
          n = ((myCount/2-1) < EMU_QUADS_BUFFER_SIZE) ? (myCount/2-1) : EMU_QUADS_BUFFER_SIZE;
          for (GLsizei ii=0; ii<(n * 6 + 2); ii++)
            indices[ii] += (EMU_QUADS_BUFFER_SIZE * 2);
        }
      }
    }
    else if ( mode == GL_QUADS )
    {
      // convert quads into quad outlines

      GLuint  indices[EMU_QUADS_BUFFER_SIZE * 8];
      GLsizei n = ((myCount/4) < EMU_QUADS_BUFFER_SIZE) ? (myCount/4) : EMU_QUADS_BUFFER_SIZE;
      if (shadeMode == GL_FLAT && gl_quads_follow_provoking_vertex_convention && (provokeMode == GL_FIRST_VERTEX_CONVENTION))
      {
        for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
        {
          indices[ii * 8 + 0] = first + ii * 4 + 0;
          indices[ii * 8 + 1] = first + ii * 4 + 1;
          indices[ii * 8 + 2] = first + ii * 4 + 0;
          indices[ii * 8 + 3] = first + ii * 4 + 3;
          indices[ii * 8 + 4] = first + ii * 4 + 1;
          indices[ii * 8 + 5] = first + ii * 4 + 2;
          indices[ii * 8 + 6] = first + ii * 4 + 3;
          indices[ii * 8 + 7] = first + ii * 4 + 2;
        }
      }
      else
      {
        for (GLuint ii=0; ii<static_cast<GLuint>(n); ii++)
        {
          indices[ii * 8 + 0] = first + ii * 4 + 1;
          indices[ii * 8 + 1] = first + ii * 4 + 0;
          indices[ii * 8 + 2] = first + ii * 4 + 0;
          indices[ii * 8 + 3] = first + ii * 4 + 3;
          indices[ii * 8 + 4] = first + ii * 4 + 1;
          indices[ii * 8 + 5] = first + ii * 4 + 2;
          indices[ii * 8 + 6] = first + ii * 4 + 2;
          indices[ii * 8 + 7] = first + ii * 4 + 3;
        }
      }

      while (myCount >= 4)
      {
        Internal("Regal::Emu::Quads::glDrawArrays","glDrawElements(GL_LINES,",n*8,",GL_UNSIGNED_INT, [])");
        dt.call(&dt.glDrawElements)(GL_LINES, n*8, GL_UNSIGNED_INT, indices);
        myCount -= (EMU_QUADS_BUFFER_SIZE * 4);

        if (myCount >= 4)
        {
          n = ((myCount/4) < EMU_QUADS_BUFFER_SIZE) ? (myCount/4) : EMU_QUADS_BUFFER_SIZE;
          for (GLsizei ii=0; ii<(n * 8); ii++)
            indices[ii] += (EMU_QUADS_BUFFER_SIZE * 4);
        }
      }
    }

    if (frontFaceMode != GL_LINE)
      dt.call(&dt.glPolygonMode)(GL_FRONT, frontFaceMode);
    if (backFaceMode != GL_LINE)
      dt.call(&dt.glPolygonMode)(GL_BACK, backFaceMode);
  }
  else if (drawPoints)
  {
    // convert quad strips or quads into points

    if (frontFaceMode != GL_POINT || backFaceMode != GL_POINT)
      dt.call(&dt.glPolygonMode)(GL_FRONT_AND_BACK, GL_POINT);

    GLsizei myCount = count &= (( mode == GL_QUADS ) ? (~0x3) : (~0x1));

    dt.call(&dt.glDrawArrays)(GL_POINTS, first, myCount);

    if (frontFaceMode != GL_POINT)
      dt.call(&dt.glPolygonMode)(GL_FRONT, frontFaceMode);
    if (backFaceMode != GL_POINT)
      dt.call(&dt.glPolygonMode)(GL_BACK, backFaceMode);
  }

  return true;
}

void Quads::glBindBuffer( GLenum target, GLuint buffer ) {
  if( target == GL_ELEMENT_ARRAY_BUFFER ) {
    elementArrayBuffer = buffer;
  }
}

void Quads::glFrontFace(GLenum mode)
{
  //<> Internal("Regal::Emu::Quads::glFrontFace(", Token::toString(mode), ")");

  switch (mode)
  {
    case GL_CW:
    case GL_CCW:
      windingMode = mode;
      break;
    default:
      break;
  }
}
void Quads::glPolygonMode(GLenum f, GLenum m)
{
  //<> Internal("Regal::Emu::Quads::glPolygonMode(", Token::toString(f), ", ", Token::toString(m), ")");

  switch (f)
  {
    case GL_FRONT:
      frontFaceMode = m;
      break;
    case GL_BACK:
      backFaceMode = m;
      break;
    case GL_FRONT_AND_BACK:
      frontFaceMode = backFaceMode = m;
      break;
    default:
      break;
  }
}

void Quads::glShadeModel(GLenum mode)
{
  //<> Internal("Regal::Emu::Quads::glShadeModel(", Token::toString(mode), ")");
  if (mode == GL_SMOOTH || mode == GL_FLAT)
    shadeMode = mode;
}

void Quads::glProvokingVertex(GLenum mode)
{
  //<> Internal("Regal::Emu::Quads::glProvokingVertex(", Token::toString(mode), ")");
  if (mode == GL_FIRST_VERTEX_CONVENTION || mode == GL_LAST_VERTEX_CONVENTION)
    provokeMode = mode;
}

void Quads::glCullFace(GLenum face)
{
  //<> Internal("Regal::Emu::Quads::glCullFace(", Token::toString(face), ")");
  switch (face)
  {
    case GL_FRONT:
    case GL_BACK:
    case GL_FRONT_AND_BACK:
      cullFace = face;
      break;
    default:
      break;
  }
}

void Quads::glEnable(GLenum cap)
{
  //<> Internal("Regal::Emu::Quads::glEnable(", Token::toString(cap), ")");
  if (cap == GL_CULL_FACE)
    cullingFaces = true;
}

void Quads::glDisable(GLenum cap)
{
  //<> Internal("Regal::Emu::Quads::glDisable(", Token::toString(cap), ")");
  if (cap == GL_CULL_FACE)
    cullingFaces = false;
}

};

REGAL_NAMESPACE_END

#endif // REGAL_EMULATION
