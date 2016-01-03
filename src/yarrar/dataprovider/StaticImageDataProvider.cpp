#include "StaticImageDataProvider.h"

namespace yarrar {

StaticImageDataProvider::StaticImageDataProvider(const std::string& imagePath):
    m_imagePath(imagePath)
{
    m_cachedImage = cv::imread(m_imagePath, cv::IMREAD_COLOR);
}

cv::Mat StaticImageDataProvider::getData()
{
    return m_cachedImage;
}

Dimensions StaticImageDataProvider::getDimensions()
{
    return {
        m_cachedImage.cols,
        m_cachedImage.rows
    };
}

}


