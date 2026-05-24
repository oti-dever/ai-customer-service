#ifndef IMAGEDATAURL_H
#define IMAGEDATAURL_H

#include <QString>

/** 将本地图片读入并编码为 data URL，供 OpenAI 兼容多模态 API 使用。 */
bool imageFileToDataUrl(const QString& absolutePath, QString* outDataUrl, QString* error);

#endif
