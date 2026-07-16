#pragma once

// Empty stub. Windows' own GL/gl.h (declares only GL 1.1) doesn't ship a
// GL/glext.h alongside it, but SystemGL.h (third_party/ImgWindow) includes
// <GL/glext.h> unconditionally on non-Apple platforms. Checked directly
// (grepped every GL_*/gl* symbol actually referenced by ImgWindow.cpp and
// ImgFontAtlas.cpp): all of it is legacy fixed-function GL 1.1 (glGenTextures,
// glTexImage2D, glVertexPointer/glDrawElements, glPush/PopAttrib, etc.),
// already declared by GL/gl.h itself -- nothing here actually needs a real
// extension header, so an empty stub satisfies the #include with zero risk
// of masking a missing declaration.
