#ifndef EDITPROFILEDIALOG_H
#define EDITPROFILEDIALOG_H

#include <QDialog>

#include "../data/userdao.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class EditProfileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditProfileDialog(const UserRecord& user, QWidget* parent = nullptr);

private slots:
    void onPickAvatar();
    void onSave();
    void onSwordCursorToggled(bool checked);
    void onPickCursorPic();
    void onResetCursorPic();

private:
    void reloadAvatarPreview();
    void reloadCursorPreferencesUi();
    QString absoluteAvatarFile() const;

    UserRecord m_user;
    QLabel* m_avatarPreview = nullptr;
    QLabel* m_loginLoginName = nullptr;
    QLineEdit* m_nicknameEdit = nullptr;
    QPlainTextEdit* m_bioEdit = nullptr;
    QPushButton* m_btnPickAvatar = nullptr;
    QPushButton* m_btnSave = nullptr;
    QPushButton* m_btnCancel = nullptr;
    QCheckBox* m_swordCursorCheck = nullptr;
    QPushButton* m_pickCursorPicBtn = nullptr;
    QPushButton* m_resetCursorPicBtn = nullptr;
    QLabel* m_cursorPicHint = nullptr;

    bool m_avatarDirty = false;
    QByteArray m_newAvatarBytes;
    QString m_newAvatarSuffix; ///< ".png" / ".jpg" for saving
};

#endif // EDITPROFILEDIALOG_H
