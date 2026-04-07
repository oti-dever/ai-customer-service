#include "editprofiledialog.h"

#include "../data/userdao.h"
#include "../utils/applystyle.h"
#include "../utils/swordcursor.h"
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

constexpr int kAvatarPreviewPx = 96;
constexpr int kMaxAvatarBytes = 5 * 1024 * 1024;
constexpr int kMaxNicknameLen = 24;
constexpr int kMaxBioLen = 200;

bool loadImageFromData(const QByteArray& data, QImage* out, QString* err)
{
    if (data.size() > kMaxAvatarBytes) {
        *err = QStringLiteral("图片过大（最大 5MB）");
        return false;
    }
    if (!out->loadFromData(data)) {
        *err = QStringLiteral("不支持的图片格式，请使用 PNG 或 JPG");
        return false;
    }
    return true;
}

} // namespace

EditProfileDialog::EditProfileDialog(const UserRecord& user, QWidget* parent,
                                     ApplyStyle::MainWindowTheme theme)
    : QDialog(parent)
    , m_user(user)
{
    setWindowTitle(QStringLiteral("编辑个人信息"));
    setObjectName(QStringLiteral("editProfileDialog"));
    setModal(true);
    resize(420, 480);
    setStyleSheet(ApplyStyle::editProfileDialogStyle(theme));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(20, 20, 20, 20);

    auto* avatarWrap = new QWidget(this);
    auto* avatarCol = new QVBoxLayout(avatarWrap);
    avatarCol->setContentsMargins(0, 0, 0, 0);
    avatarCol->setSpacing(10);
    m_avatarPreview = new QLabel(avatarWrap);
    m_avatarPreview->setObjectName(QStringLiteral("editProfileAvatar"));
    m_avatarPreview->setFixedSize(kAvatarPreviewPx, kAvatarPreviewPx);
    m_avatarPreview->setAlignment(Qt::AlignCenter);
    m_avatarPreview->setScaledContents(true);
    m_btnPickAvatar = new QPushButton(QStringLiteral("选择图片…"), avatarWrap);
    connect(m_btnPickAvatar, &QPushButton::clicked, this, &EditProfileDialog::onPickAvatar);
    avatarCol->addWidget(m_avatarPreview, 0, Qt::AlignHCenter);
    avatarCol->addWidget(m_btnPickAvatar, 0, Qt::AlignHCenter);
    root->addWidget(avatarWrap, 0, Qt::AlignHCenter);

    auto* form = new QFormLayout();
    m_loginLoginName = new QLabel(m_user.username, this);
    m_loginLoginName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("登录名（不可改）"), m_loginLoginName);

    m_nicknameEdit = new QLineEdit(this);
    m_nicknameEdit->setPlaceholderText(QStringLiteral("留空则侧栏显示登录名"));
    m_nicknameEdit->setMaxLength(kMaxNicknameLen);
    m_nicknameEdit->setText(m_user.displayName);
    form->addRow(QStringLiteral("昵称"), m_nicknameEdit);

    m_bioEdit = new QPlainTextEdit(this);
    m_bioEdit->setObjectName(QStringLiteral("editProfileBio"));
    m_bioEdit->setPlaceholderText(QStringLiteral("个性签名（可选，最多 200 字）"));
    m_bioEdit->setPlainText(m_user.bio);
    m_bioEdit->setTabChangesFocus(true);
    m_bioEdit->setFixedHeight(100);
    form->addRow(QStringLiteral("个性签名"), m_bioEdit);
    root->addLayout(form);

    m_swordCursorCheck = new QCheckBox(
        QStringLiteral("使用剑形光标（除登录界面外全局生效，默认关闭）"), this);
    m_swordCursorCheck->setChecked(SwordCursor::isEnabledInSettings());
    connect(m_swordCursorCheck, &QCheckBox::toggled, this, &EditProfileDialog::onSwordCursorToggled);
    root->addWidget(m_swordCursorCheck);

    auto* curRow = new QHBoxLayout();
    m_pickCursorPicBtn = new QPushButton(QStringLiteral("选择光标图片…"), this);
    m_resetCursorPicBtn = new QPushButton(QStringLiteral("恢复内置图"), this);
    connect(m_pickCursorPicBtn, &QPushButton::clicked, this, &EditProfileDialog::onPickCursorPic);
    connect(m_resetCursorPicBtn, &QPushButton::clicked, this, &EditProfileDialog::onResetCursorPic);
    curRow->addWidget(m_pickCursorPicBtn);
    curRow->addWidget(m_resetCursorPicBtn);
    curRow->addStretch();
    root->addLayout(curRow);
    m_cursorPicHint = new QLabel(this);
    m_cursorPicHint->setObjectName(QStringLiteral("editProfileHintMuted"));
    m_cursorPicHint->setWordWrap(true);
    root->addWidget(m_cursorPicHint);
    {
        auto* cursorTip = new QLabel(
            QStringLiteral("提示：自定义光标请使用透明底 PNG/JPG。"),
            this);
        cursorTip->setObjectName(QStringLiteral("editProfileHintFaint"));
        cursorTip->setWordWrap(true);
        root->addWidget(cursorTip);
    }
    reloadCursorPreferencesUi();

    root->addStretch();

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_btnCancel = new QPushButton(QStringLiteral("取消"), this);
    m_btnSave = new QPushButton(QStringLiteral("保存"), this);
    m_btnSave->setDefault(true);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnSave, &QPushButton::clicked, this, &EditProfileDialog::onSave);
    btnRow->addWidget(m_btnCancel);
    btnRow->addWidget(m_btnSave);
    root->addLayout(btnRow);

    reloadAvatarPreview();
}

void EditProfileDialog::reloadCursorPreferencesUi()
{
    const QString p = SwordCursor::customImagePath();
    if (p.isEmpty())
        m_cursorPicHint->setText(QStringLiteral("光标图：内置 sword_cursor.png（PNG）"));
    else
        m_cursorPicHint->setText(QStringLiteral("光标图：%1").arg(p));
}

void EditProfileDialog::onSwordCursorToggled(bool checked)
{
    SwordCursor::setEnabledInSettings(checked);
    if (checked)
        SwordCursor::applyIfEnabled();
    else
        SwordCursor::restore();
}

void EditProfileDialog::onPickCursorPic()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择光标图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QImage probe(path);
    if (probe.isNull()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("无法加载该图片，请使用 PNG 或 JPG"));
        return;
    }
    SwordCursor::setCustomImagePath(path);
    reloadCursorPreferencesUi();
    if (SwordCursor::isEnabledInSettings())
        SwordCursor::applyIfEnabled();
}

void EditProfileDialog::onResetCursorPic()
{
    SwordCursor::setCustomImagePath(QString());
    reloadCursorPreferencesUi();
    if (SwordCursor::isEnabledInSettings())
        SwordCursor::applyIfEnabled();
}

QString EditProfileDialog::absoluteAvatarFile() const
{
    if (m_user.avatarPath.isEmpty())
        return {};
    return UserDao::absolutePathFromProjectRelative(m_user.avatarPath);
}

void EditProfileDialog::reloadAvatarPreview()
{
    QPixmap pm;
    if (m_avatarDirty && !m_newAvatarBytes.isEmpty()) {
        QImage img;
        QString err;
        if (loadImageFromData(m_newAvatarBytes, &img, &err)) {
            pm = QPixmap::fromImage(img.scaled(kAvatarPreviewPx, kAvatarPreviewPx, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        }
    } else {
        const QString abs = absoluteAvatarFile();
        if (!abs.isEmpty() && QFile::exists(abs)) {
            QImage img(abs);
            if (!img.isNull()) {
                pm = QPixmap::fromImage(img.scaled(kAvatarPreviewPx, kAvatarPreviewPx, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
            }
        }
    }
    if (pm.isNull())
        m_avatarPreview->clear();
    else
        m_avatarPreview->setPixmap(pm);
}

void EditProfileDialog::onPickAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("无法读取文件"));
        return;
    }
    QByteArray data = f.readAll();
    QImage img;
    QString err;
    if (!loadImageFromData(data, &img, &err)) {
        QMessageBox::warning(this, QStringLiteral("提示"), err);
        return;
    }
    m_newAvatarBytes = std::move(data);
    const QString suf = QFileInfo(path).suffix().toLower();
    m_newAvatarSuffix = (suf == QStringLiteral("jpg") || suf == QStringLiteral("jpeg"))
        ? QStringLiteral(".jpg")
        : QStringLiteral(".png");
    m_avatarDirty = true;
    reloadAvatarPreview();
}

void EditProfileDialog::onSave()
{
    const QString nick = m_nicknameEdit->text().trimmed();
    if (nick.length() > kMaxNicknameLen) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("昵称过长"));
        return;
    }
    const QString bio = m_bioEdit->toPlainText().trimmed();
    if (bio.length() > kMaxBioLen) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("个性签名过长（最多 200 字）"));
        return;
    }

    QString relPath = m_user.avatarPath;
    if (m_avatarDirty) {
        if (!UserDao::ensureAvatarsDirectory()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("无法创建头像目录"));
            return;
        }
        const QString rel = UserDao::relativeAvatarPathForUserId(m_user.id, m_newAvatarSuffix);
        const QString abs = UserDao::absolutePathFromProjectRelative(rel);
        QImage img;
        if (!img.loadFromData(m_newAvatarBytes)) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("图片无效"));
            return;
        }
        img = img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const char* fmt = m_newAvatarSuffix == QStringLiteral(".jpg") ? "JPEG" : "PNG";
        if (!img.save(abs, fmt, fmt == QStringLiteral("JPEG") ? 88 : -1)) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("保存头像失败"));
            return;
        }
        relPath = rel;
    }

    UserDao dao;
    if (!dao.updateProfile(m_user.id, nick, bio, relPath)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("保存失败，请稍后重试"));
        return;
    }
    accept();
}
