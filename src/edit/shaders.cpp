// std
#include <format>
#include <memory>
#include <stdexcept>
// swg
#include "resource.hpp"
#include "shaders.hpp"

namespace swg::details {

namespace {

unique_gl_shader compile_shader(GLenum type, const char* source) {
  unique_gl_shader shader{glCreateShader(type)};
  glShaderSource(shader.get(), 1, &source, nullptr);
  glCompileShader(shader.get());
  GLint ok;
  glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024]{};
    glGetShaderInfoLog(shader.get(), static_cast<int>(std::size(log)), nullptr, log);
    throw std::runtime_error{std::format("gl shader compile error: {}", log).c_str()};
  }
  return shader;
}

}  // namespace

unique_gl_program create_gl_program() {
  const char* vs_source = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aUV;
uniform vec2 uViewport;
out vec3 vUV;
void main() {
  vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
  ndc.y = -ndc.y;
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUV = aUV;
}
)";
  const char* fs_source = R"(
#version 330 core
in vec3 vUV;
uniform sampler2DArray uAtlas;
uniform vec2 uAtlasSize;
out vec4 oColor;
void main() {
  vec2 normUV = vUV.xy / uAtlasSize;
  float alpha = texture(uAtlas, vec3(normUV, vUV.z)).r;
  oColor = vec4(0.0, 0.0, 0.0, alpha);
}
)";

  auto vs = compile_shader(GL_VERTEX_SHADER, vs_source);
  auto fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
  unique_gl_program prog{glCreateProgram()};
  glAttachShader(prog.get(), vs.get());
  glAttachShader(prog.get(), fs.get());
  glLinkProgram(prog.get());
  GLint ok;
  glGetProgramiv(prog.get(), GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024]{};
    glGetProgramInfoLog(prog.get(), static_cast<int>(std::size(log)), nullptr, log);
  }
  return prog;
}

}  // namespace swg::details
