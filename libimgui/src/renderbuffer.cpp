#include "renderbuffer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

RenderBuffer::RenderBuffer(int w, int h)
{
    m_height = h;
    m_width = w;
    m_rendertexture = 0;
    m_rot_x = m_rot_y = 0;
    m_dist_z = -10;
    m_pos[0] = m_pos[1] = m_pos[2] = 0.f;
    m_fov = 45.f;
    m_smooth_normal = true;

    glGenFramebuffers(1, &m_fbo);
    glGenTextures(1, &m_rendertexture);
    glGenRenderbuffers(1, &m_depthrenderbuffer);
}

RenderBuffer::~RenderBuffer()
{
    if (m_rendertexture){
        glDeleteTextures(1, &m_rendertexture);
    }
    if (m_depthrenderbuffer){
        glDeleteRenderbuffers(1, &m_depthrenderbuffer);
    }
    if (m_fbo){
        glDeleteFramebuffers(1, &m_fbo);
    }
}

void RenderBuffer::change_buffer_size(int w, int h)
{
    if (w != m_width || h != m_height){
        m_size_dirty = true;
    }
    m_height = h;
    m_width = w;
    
}

void RenderBuffer::change_pos(float x, float y)
{
    float scale = ( tanf(glm::radians(m_fov*0.5f)) * 2.0f * -m_dist_z ) / m_height;
    x *= scale;
    y *= scale;

    glm::mat4 rot = glm::mat4(1.0f);
    rot = glm::translate(rot, glm::vec3(-x, y, 0.f));
    rot = glm::rotate(rot,m_rot_y * 3.14f / 180.f  , glm::vec3(1.0f, 0.0f, 0.0f));
    rot = glm::rotate(rot,m_rot_x * 3.14f / 180.f  , glm::vec3(0.0f, 1.0f, 0.0f));
    rot = glm::inverse(rot);

    glm::vec3 x_comp(rot[0][0], rot[0][1], rot [0][2]);
    glm::vec3 y_comp(rot[1][0], rot[1][1], rot [1][2]);

    m_pos += glm::vec3(rot[3][0], rot[3][1], rot[3][2]);
}

void RenderBuffer::draw(bool draw_nurbs, bool draw_mesh, bool draw_strands)
{
    if (m_size_dirty){
        glBindTexture(GL_TEXTURE_2D, m_rendertexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    if (m_size_dirty){
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthrenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, m_width, m_height);
    }

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthrenderbuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_rendertexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, m_width, m_height);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);
        
        view = glm::translate(view, glm::vec3(0.0f, 0.0f, m_dist_z));
        view = glm::rotate(view,m_rot_y * 3.14f / 180.f  , glm::vec3(1.0f, 0.0f, 0.0f));
        view = glm::rotate(view,m_rot_x * 3.14f / 180.f  , glm::vec3(0.0f, 1.0f, 0.0f));
        view = glm::translate(view, glm::vec3(m_pos[0], m_pos[1], m_pos[2]));

        projection = glm::perspective(glm::radians(m_fov), (float)m_width / (float)m_height, 0.1f, 10000.0f);

        glm::mat4 modelview = view;
        glm::mat3 rotMat(modelview);
        glm::vec3 d(modelview[3]);
        glm::vec3 campos = -d * rotMat;

        glEnable(GL_DEPTH_TEST);
        // Draw here

    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);   
}
