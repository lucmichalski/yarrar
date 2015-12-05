#include "OpenGLRenderer.h"
#include "GLProgram.h"
#include "GLShader.h"
#include "BackgroundModel.h"
#include "CubeModel.h"

namespace {

    using namespace yarrar;

#ifdef YARRAR_OPENGL_CONTEXT
    std::string SHADER_PATH = "/home/niki/programming/yarrar/data/shader/gl3/";
#elif YARRAR_OPENGLES_CONTEXT
    std::string SHADER_PATH = "/sdcard/yarrar/shader/";
#endif

    const std::vector<ShaderDef> VERTEX_SHADERS
    {
       {"camera", SHADER_PATH + "camera.vertex"},
       {"simple", SHADER_PATH + "simple.vertex"}
    };

    const std::vector<ShaderDef> FRAGMENT_SHADERS
    {
        {"simple", SHADER_PATH + "simple.fragment"},
        {"texture", SHADER_PATH + "texture.fragment"}
    };

    const std::vector<EffectDef> EFFECTS
    {
        {"cube", "camera", "simple", true},
        {"bg", "simple", "texture", false}
    };

}

namespace yarrar {

OpenGLRenderer::OpenGLRenderer(int width, int height):
    m_context(new GLContext(width, height)),
    m_backgroundTex(0)
{
    for(const auto& def : VERTEX_SHADERS)
    {
        m_vertexShaders[def.name] = std::unique_ptr<GLShader>(new GLShader(def, GL_VERTEX_SHADER));
    }

    for(const auto& def : FRAGMENT_SHADERS)
    {
        m_fragmentShaders[def.name] = std::unique_ptr<GLShader>(new GLShader(def, GL_FRAGMENT_SHADER));
    }

    for(const auto& def : EFFECTS)
    {
        m_programs[def.name] =
                std::shared_ptr<GLProgram>(new GLProgram(
                                                   m_vertexShaders[def.vertexShader].get(),
                                                   m_fragmentShaders[def.fragmentShader].get(),
                                                   def.perspectiveProjection));
    }

    m_bgModel.reset(new BackgroundModel(m_programs["bg"]));
    m_cubeModel.reset(new CubeModel(m_programs["cube"]));

    glGenTextures(1, &m_backgroundTex);
}

OpenGLRenderer::~OpenGLRenderer()
{
}

void OpenGLRenderer::render(bool renderBackground, bool renderWorld)
{
    // Clear screen.
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(renderBackground)
    {
        // Disable depth buffering for background rendering.
        glDisable(GL_DEPTH_TEST);
        m_bgModel->setTexture(m_backgroundTex);
        m_bgModel->render();
    }

    if(renderWorld)
    {
        // Depth buffering back on for 3D models.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        m_cubeModel->render();
    }

    m_context->swapBuffers();
}

void OpenGLRenderer::loadImage(const cv::Mat& image)
{
    // OpenCV stores images flipped in in comparison to OpenGL.
    cv::Mat flipped;
    cv::flip(image, flipped, 0);
    glBindTexture(GL_TEXTURE_2D, m_backgroundTex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D,
                 0, // Mip level
                 GL_RGB,
                 flipped.cols, // Width
                 flipped.rows, // Height
                 0, // Border
#ifdef YARRAR_OPENGL_CONTEXT
                 GL_BGR, // Input format: OpenCV is using BGR.
#elif YARRAR_OPENGLES_CONTEXT
                 GL_RGB, // Input format: OpenCV is using BGR.
                 // TODO: GLES doesn't have BGR
#endif
                 GL_UNSIGNED_BYTE, // Data type
                 flipped.ptr());
}

void OpenGLRenderer::draw(const yarrar::Pose &cameraPose, const cv::Mat& image)
{
    if(cameraPose.valid)
    {
        //glfwPollEvents();

        // Convert rotation from vector to matrix.
        cv::Mat rotation;
        cv::Rodrigues(cameraPose.rotation, rotation);

        cv::Mat viewMatrix(cv::Mat::zeros(4, 4, CV_32F));
        for(int row = 0; row < 3; ++row)
        {
            for(int col = 0; col < 3; ++col)
            {
                viewMatrix.at<float>(row, col) = (float) rotation.at<double>(row, col);
            }

            viewMatrix.at<float>(row, 3) = (float) cameraPose.translation.at<double>(row, 0);
        }
        viewMatrix.at<float>(3, 3) = 1.0f;

        // Invert y and z axes.
        cv::Mat openCVToOpenGL = cv::Mat::zeros(4, 4, CV_32F);
        openCVToOpenGL.at<float>(0, 0) = 1.0f;
        openCVToOpenGL.at<float>(1, 1) = -1.0f;
        openCVToOpenGL.at<float>(2, 2) = -1.0f;
        openCVToOpenGL.at<float>(3, 3) = 1.0f;
        viewMatrix = openCVToOpenGL * viewMatrix;

        // OpenCV is row major, OpenGL column major.
        cv::Mat glViewMatrix = cv::Mat::zeros(4, 4, CV_32F);
        cv::transpose(viewMatrix, glViewMatrix);

        // Calculate projection matrix.
        float fx = cameraPose.camera.at<float>(0,0);
        float cx = cameraPose.camera.at<float>(0,2);
        float fy = cameraPose.camera.at<float>(1,1);
        float cy = cameraPose.camera.at<float>(1,2);
        float far = 10.0f;
        float near = 0.1f;
        
        float projection[4][4];
        memset(projection, 0, sizeof(float) * 16);

        projection[0][0] = fx / cx;
        projection[1][1] = fy / cy;
        projection[2][2] = (-1.0f * (far + near) / (far - near));
        projection[3][2] = (-2.0f * far * near) / (far - near);
        projection[2][3] = -1;
        projection[3][3] = 0;

        for(const auto& program : m_programs)
        {
            if(program.second->getUsePerspectiveProjection())
            {
                ScopedUseProgram p_(program.second.get());
                program.second->setUniformMatrix4fv("camera", (GLfloat *) glViewMatrix.data);
                // TODO: Projection should only be changed when necessary.
                program.second->setUniformMatrix4fv("projection", (GLfloat *) projection);
            }
        }
    }

    // Load fresh background camera image.
    loadImage(image);
    render(true, cameraPose.valid);
}

}