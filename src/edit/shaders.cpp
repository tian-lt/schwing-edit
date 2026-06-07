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
  GLint status;
  glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &status);
  if (!status) {
    char log[512]{};
    glGetShaderInfoLog(shader.get(), static_cast<int>(std::size(log)), nullptr, log);
    throw std::runtime_error{std::format("gl shader compile error: {}", log).c_str()};
  }
  return shader;
}

}  // namespace

unique_gl_program create_gl_program() {
  const char* vs_source = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
void main() {
  gl_Position = vec4(aPos, 1.0);
}
)";
  const char* fs_source = R"(
#version 330 core
out vec4 FragColor;
void main() {
  FragColor = vec4(1.0, 0.5, 0.2, 1.0);
}
)";

  auto vs = compile_shader(GL_VERTEX_SHADER, vs_source);
  auto fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
  unique_gl_program prog{glCreateProgram()};
  glAttachShader(prog.get(), vs.get());
  glAttachShader(prog.get(), fs.get());
  glLinkProgram(prog.get());
  GLint status;
  glGetProgramiv(prog.get(), GL_LINK_STATUS, &status);
  if (!status) {
    char log[512]{};
    glGetProgramInfoLog(prog.get(), static_cast<int>(std::size(log)), nullptr, log);
  }
  return prog;
}

}  // namespace swg::details
