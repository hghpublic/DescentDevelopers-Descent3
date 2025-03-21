/*
* Descent 3
* Copyright (C) 2024 Descent Developers
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "dyna_gl.h"
#include "holder.h"

template <typename E>
struct VertexAttrib {
  using EnclosingType = E;
  GLint size;
  GLenum type;
  GLboolean normalized;
  uintptr_t offset;
  std::string name;
};

template <typename EnclosingType, typename MemberType>
VertexAttrib<EnclosingType> vertexAttrib(GLint size, GLenum type, GLboolean normalized, MemberType EnclosingType::*member, std::string name) {
  EnclosingType e;
  return VertexAttrib<EnclosingType>{
    size,
    type,
    normalized,
    reinterpret_cast<uintptr_t>(&(e.*member)) - reinterpret_cast<uintptr_t>(&e),
    std::move(name)
  };
}

template <typename V>
struct VertexBuffer {
  VertexBuffer(GLuint program,
               std::vector<VertexAttrib<V>> attribs,
               size_t vertexCount,
               GLenum bufferType,
               V const* initialData = nullptr)
    : vao_{outval(dglGenVertexArrays)},
      vbo_{outval(dglGenBuffers)} {
    dglBindVertexArray(vao_);

    bind();
    dglBufferData(GL_ARRAY_BUFFER,
                  vertexCount * sizeof(V),
                  initialData,
                  bufferType);

    for (std::size_t i{}; i < attribs.size(); i++) {
      dglEnableVertexAttribArray(i);
      dglVertexAttribPointer(i,
                             attribs[i].size,
                             attribs[i].type,
                             attribs[i].normalized,
                             sizeof(V),
                             reinterpret_cast<void*>(attribs[i].offset));
      dglBindAttribLocation(program, i, attribs[i].name.c_str());
    }
  }

  void UpdateData(size_t vtx_offset, size_t vtx_count, V const* vertices) {
    bind();
    dglBufferSubData(GL_ARRAY_BUFFER, vtx_offset * sizeof(V), vtx_count * sizeof(V), vertices);
  }

protected:
  void bind() {
    dglBindBuffer(GL_ARRAY_BUFFER, vbo_);
  }

private:
  static void DeleteBuffer(GLuint id) {
    dglDeleteBuffers(1, &id);
  }
  static void DeleteVertexArray(GLuint id) {
    dglDeleteVertexArrays(1, &id);
  }
  template <typename Generator>
  GLuint outval(Generator&& gen) {
    GLuint id;
    gen(1, &id);
    return id;
  }

  MoveOnlyHolder<GLuint, DeleteVertexArray> vao_;
  MoveOnlyHolder<GLuint, DeleteBuffer> vbo_;
};

// https://www.khronos.org/opengl/wiki/Buffer_Object_Streaming#Buffer_update
template <typename V>
struct OrphaningVertexBuffer : VertexBuffer<V> {
  OrphaningVertexBuffer(GLuint program, std::vector<VertexAttrib<V>> attribs)
    : VertexBuffer<V>{program, std::move(attribs), kVertexCount, kBufferType} {}

  template <typename VertexIter,
            typename = std::enable_if<std::is_convertible_v<typename std::iterator_traits<VertexIter>::value_type, V>>>
  size_t AddVertexData(VertexIter begin, VertexIter end) {
    this->bind();

    auto dist = std::distance(begin, end);
    if (nextVertex_ + dist >= kVertexCount) {
      dglBufferData(GL_ARRAY_BUFFER,
              kVertexCount * sizeof(V),
              nullptr,
              kBufferType);
      nextVertex_ = 0;
    }

    auto start = nextVertex_;
    V* mapped = reinterpret_cast<V*>(dglMapBufferRange(GL_ARRAY_BUFFER, start * sizeof(V), dist * sizeof(V), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT));
    std::copy(begin, end, mapped);
    dglUnmapBuffer(GL_ARRAY_BUFFER);

    nextVertex_ += dist;
    return start;
  }

private:
  static constexpr size_t kVertexCount{1 << 16};
  static constexpr GLenum kBufferType{GL_STREAM_DRAW};
  size_t nextVertex_{};
};

template <GLenum kType>
struct Shader {
  static_assert(kType == GL_VERTEX_SHADER || kType == GL_FRAGMENT_SHADER);

  explicit Shader(std::string_view src)
      : id_{dglCreateShader(kType)} {
    if (id_ == 0) {
      throw std::runtime_error("failed to create shader");
    }

    char const* srcptr = src.data();
    GLint srclen = src.size();
    dglShaderSource(id_, 1, &srcptr, &srclen);
    dglCompileShader(id_);

    GLint compile_result;
    dglGetShaderiv(id_, GL_COMPILE_STATUS, &compile_result);
    if (compile_result != GL_TRUE) {
      GLsizei log_length = 0;
      GLchar message[1024];
      dglGetShaderInfoLog(id_, 1024, &log_length, message);
      throw std::runtime_error(std::string{message, static_cast<size_t>(log_length)});
    }
  }

  GLuint id() const {
    ASSERT(id_ != 0);
    return id_;
  }

private:
  static void DeleteShader(GLuint id) {
    dglDeleteShader(id);
  }
  MoveOnlyHolder<GLuint, DeleteShader> id_;
};

template <typename V>
struct ShaderProgram {
  explicit ShaderProgram(std::string_view vertexSrc, std::string_view fragmentSrc, std::vector<VertexAttrib<V>> attribs)
    : id_{dglCreateProgram()}, vertex_{vertexSrc}, fragment_{fragmentSrc}, vbo_{id_, std::move(attribs)} {
    if (id_ == 0) {
      throw std::runtime_error("error creating GL program");
    }

    dglAttachShader(id_, vertex_.id());
    dglAttachShader(id_, fragment_.id());

    dglLinkProgram(id_);

    GLint link_result;
    dglGetProgramiv(id_, GL_LINK_STATUS, &link_result);
    if (link_result != GL_TRUE)
    {
      GLsizei log_length = 0;
      GLchar message[1024];
      dglGetProgramInfoLog(id_, 1024, &log_length, message);
      throw std::runtime_error(std::string{message, static_cast<size_t>(log_length)});
    }
  }

  void Use() const {
    dglUseProgram(id_);
  }

  void Unuse() const {
    dglUseProgram(0);
  }

  template <typename VertexIter,
            typename = std::enable_if<std::is_convertible_v<typename std::iterator_traits<VertexIter>::value_type, V>>>
  size_t addVertexData(VertexIter begin, VertexIter end) {
    return vbo_.AddVertexData(begin, end);
  }

  void setUniformMat4f(std::string const& name, glm::mat4x4 const& matrix) {
    dglUniformMatrix4fv(getUniformId(name), 1, GL_FALSE, glm::value_ptr(matrix));
  }

  void setUniform1i(std::string const& name, GLint val) {
    dglUniform1i(getUniformId(name), val);
  }

  void setUniform1f(std::string const& name, GLfloat val) {
    dglUniform1f(getUniformId(name), val);
  }

  void setUniform4fv(std::string const& name, GLfloat f0, GLfloat f1, GLfloat f2, GLfloat f3) {
    dglUniform4f(getUniformId(name), f0, f1, f2, f3);
  }

private:
  GLint getUniformId(std::string const& name) {
    auto it = uniform_cache_.find(name);
    if (it != uniform_cache_.end()) {
      return it->second;
    }

    it = uniform_cache_.emplace(name, dglGetUniformLocation(id_, name.c_str())).first;
    if (it->second != -1) {
      return it->second;
    }

    throw std::runtime_error("uniform " + name + " nonexistent or inactive");
  }

  static void DeleteProgram(GLuint id) {
    dglDeleteProgram(id);
  }

  MoveOnlyHolder<GLuint, DeleteProgram> id_;
  Shader<GL_VERTEX_SHADER> vertex_;
  Shader<GL_FRAGMENT_SHADER> fragment_;
  OrphaningVertexBuffer<V> vbo_;
  std::unordered_map<std::string, GLint> uniform_cache_;
};
