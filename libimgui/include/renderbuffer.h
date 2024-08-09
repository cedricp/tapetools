#pragma once

#include <GL/glew.h>
#include <vector>
#include <utility>

#include <glm/glm.hpp>

class RenderBuffer
{
    GLuint m_rendertexture = 0;
    GLuint m_depthrenderbuffer = 0, m_fbo = 0;
    bool m_size_dirty = true;
    int m_height, m_width;
    float m_rot_y;
    float m_rot_x;
    float m_dist_z;
    float m_fov;
    glm::vec3 m_pos;
    bool m_smooth_normal;

public:
    RenderBuffer(int w, int h);
    ~RenderBuffer();

    void* get_rendertexture(){return (void*)(size_t)m_rendertexture;}
    void change_rot(float x, float y){
        m_rot_x += x * .5f;
        m_rot_y += y * .5;
    }
    void change_z(float z){
        m_dist_z = z;
    }
    void change_pos(float x, float y);

    void draw(bool nurbs=true, bool mesh=true, bool strands=true);
    void change_buffer_size(int, int);
    bool& smooth_attr(){return m_smooth_normal;}
    float& fov_attr(){return m_fov;}
};