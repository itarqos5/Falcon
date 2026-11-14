#include "falcon/gfx/shader.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace falcon {
namespace {

gl::GLuint compile(gl::GLenum type, const char* source, const char* name) {
    const gl::GLuint shader = gl::CreateShader(type);
    gl::ShaderSource(shader, 1, &source, nullptr);
    gl::CompileShader(shader);

    gl::GLint ok = 0;
    gl::GetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (ok == 0) {
        gl::GLint len = 0;
        gl::GetShaderiv(shader, gl::INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<std::size_t>(len > 1 ? len : 1));
        gl::GetShaderInfoLog(shader, len, nullptr, log.data());
        gl::DeleteShader(shader);
        throw std::runtime_error(std::string("shader '") + name + "' failed to compile: " +
                                 log.data());
    }
    return shader;
}

}  // namespace

ShaderProgram::ShaderProgram(const char* name, const char* vertexSource,
                             const char* fragmentSource) {
    const gl::GLuint vs = compile(gl::VERTEX_SHADER, vertexSource, name);
    const gl::GLuint fs = compile(gl::FRAGMENT_SHADER, fragmentSource, name);

    id_ = gl::CreateProgram();
    gl::AttachShader(id_, vs);
    gl::AttachShader(id_, fs);
    gl::LinkProgram(id_);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    gl::GLint ok = 0;
    gl::GetProgramiv(id_, gl::LINK_STATUS, &ok);
    if (ok == 0) {
        gl::GLint len = 0;
        gl::GetProgramiv(id_, gl::INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<std::size_t>(len > 1 ? len : 1));
        gl::GetProgramInfoLog(id_, len, nullptr, log.data());
        gl::DeleteProgram(id_);
        id_ = 0;
        throw std::runtime_error(std::string("shader '") + name + "' failed to link: " +
                                 log.data());
    }
}

ShaderProgram::~ShaderProgram() {
    if (id_ != 0) gl::DeleteProgram(id_);
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) gl::DeleteProgram(id_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void ShaderProgram::use() const { gl::UseProgram(id_); }

gl::GLint ShaderProgram::uniform(const char* name) const {
    return gl::GetUniformLocation(id_, name);
}

}  // namespace falcon
