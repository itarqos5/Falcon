#pragma once

#include "apex/gfx/gl.hpp"

namespace apex {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ShaderProgram(const char* name, const char* vertexSource, const char* fragmentSource);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    void use() const;
    [[nodiscard]] gl::GLint uniform(const char* name) const;

private:
    gl::GLuint id_ = 0;
};

}  // namespace apex
