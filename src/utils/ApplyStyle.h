#ifndef APPLYSTYLE_H
#define APPLYSTYLE_H

#include <QString>

class ApplyStyle {
public:
    static QString loginWindowStyle();
    static QString mainWindowStyle();
    static QString aggregateChatFormStyle();
    static QString helpDialogStyle();

private:
    ApplyStyle() = delete;
    ~ApplyStyle() = delete;
};

#endif // APPLYSTYLE_H
