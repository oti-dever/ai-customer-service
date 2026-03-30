#pragma once

#include <QComboBox>
#include <QStyle>

/**
 * 下拉框关闭 / 弹出时切换 dynamic property「popupOpen」，供 QSS 分别使用
 * fold_arrow_to_expand_icon.svg / fold_arrow_to_collapse_icon.svg。
 */
class FoldArrowComboBox : public QComboBox
{
public:
    explicit FoldArrowComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
    }

protected:
    void showPopup() override
    {
        setProperty("popupOpen", true);
        style()->unpolish(this);
        style()->polish(this);
        QComboBox::showPopup();
    }

    void hidePopup() override
    {
        QComboBox::hidePopup();
        setProperty("popupOpen", false);
        style()->unpolish(this);
        style()->polish(this);
    }
};
