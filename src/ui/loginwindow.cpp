#include "loginwindow.h"
#include "mainwindow.h"
#include "../core/authmanager.h"
#include "../utils/applystyle.h"
#include "../data/userdao.h"
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QBitmap>
#include <QColor>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QSizePolicy>

namespace {

constexpr int kLoginWindowCornerRadius = 14;
constexpr int kAvatarSide = 96;


class LoginTitleBar final : public QWidget
{
public:
    explicit LoginTitleBar(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("loginTitleBar"));
        setFixedHeight(40);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        auto* title = new QLabel(QStringLiteral("AI客服 - 登录"), this);
        title->setObjectName(QStringLiteral("loginTitleBarLabel"));
        title->setAttribute(Qt::WA_TranslucentBackground);
        title->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto* stretch = new QWidget(this);
        stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        stretch->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("loginCloseBtn"));
        m_close->setIcon(QIcon(QStringLiteral(":/close_window_icon.svg")));
        m_close->setIconSize(QSize(14, 14));
        m_close->setFixedSize(48, 40);
        m_close->setCursor(Qt::PointingHandCursor);
        m_close->setToolTip(QStringLiteral("关闭"));
        m_close->setFocusPolicy(Qt::NoFocus);
        m_close->setAutoRaise(true);
        connect(m_close, &QToolButton::clicked, this, [this] {
            if (auto* d = qobject_cast<QDialog*>(window()))
                d->reject();
        });

        lay->addWidget(title, 0, Qt::AlignVCenter);
        lay->addWidget(stretch, 1);
        lay->addWidget(m_close, 0, Qt::AlignTop | Qt::AlignRight);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(e);
            return;
        }
        m_dragging = true;
        if (QWidget* w = window())
            m_dragOffset = e->globalPosition().toPoint() - w->frameGeometry().topLeft();
        grabMouse();
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            if (QWidget* w = window())
                w->move(e->globalPosition().toPoint() - m_dragOffset);
            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging = false;
            releaseMouse();
        }
        QWidget::mouseReleaseEvent(e);
    }

private:
    QToolButton* m_close = nullptr;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

} // namespace

LoginWindow::LoginWindow(QWidget* parent)
    : QDialog(parent)
{
    m_auth = new AuthManager(this);
    setupUI();
    setupStyles();
    showLoginForm();

    connect(m_auth, &AuthManager::loginSucceeded, this, [this](const QString& username) {
        m_loggedInUsername = username;
        accept();
    });
    connect(m_auth, &AuthManager::loginFailed, this, &LoginWindow::showError);
    connect(m_auth, &AuthManager::registerSucceeded, this, [this]() {
        QString registeredUsername = m_registerUsernameEdit->text().trimmed();
        showLoginForm();
        m_usernameEdit->setText(registeredUsername);
        m_passwordEdit->setFocus();
        m_errorLabel->setStyleSheet("color: #52c41a;");
        m_errorLabel->setText(QStringLiteral("注册成功，请登录"));
        m_errorLabel->setVisible(true);
    });
    connect(m_auth, &AuthManager::registerFailed, this, &LoginWindow::showError);
}

LoginWindow::~LoginWindow()
{
}

void LoginWindow::setupUI()
{
    setWindowTitle(QStringLiteral("AI客服 - 登录"));
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.svg")));
    setFixedSize(440, 592);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(new LoginTitleBar(this));

    m_card = new QFrame(this);
    m_card->setObjectName("loginCard");
    m_card->setFixedWidth(350);
    m_card->setMinimumHeight(400);

    QVBoxLayout* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(32, 40, 32, 40);
    cardLayout->setSpacing(16);

    m_avatarLabel = new QLabel(m_card);
    m_avatarLabel->setObjectName(QStringLiteral("loginAvatar"));
    m_avatarLabel->setFixedSize(kAvatarSide, kAvatarSide);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_avatarLabel, 0, Qt::AlignHCenter);
    cardLayout->addSpacing(4);

    m_titleLabel = new QLabel("欢迎登录", m_card);
    m_titleLabel->setObjectName("loginTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_titleLabel);

    QLabel* subtitleLabel = new QLabel("请输入您的账号信息", m_card);
    subtitleLabel->setObjectName("loginSubtitle");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(subtitleLabel);

    cardLayout->addSpacing(8);

    m_errorLabel = new QLabel(m_card);
    m_errorLabel->setObjectName("errorLabel");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    cardLayout->addWidget(m_errorLabel);

    m_loginForm = new QWidget(m_card);
    m_registerForm = new QWidget(m_card);
    buildLoginForm();
    buildRegisterForm();

    connect(m_usernameEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        refreshLoginAvatarForUsernameField();
    });

    cardLayout->addWidget(m_loginForm);
    cardLayout->addWidget(m_registerForm);
    m_registerForm->setVisible(false);

    cardLayout->addStretch();

    mainLayout->addStretch();
    updateWelcomeTitle();
    mainLayout->addWidget(m_card, 0, Qt::AlignCenter);
    mainLayout->addStretch();

    updateRoundedWindowMask();

    for (QLineEdit* ed : { m_usernameEdit, m_passwordEdit, m_registerUsernameEdit,
                          m_registerPasswordEdit, m_registerConfirmEdit }) {
        ed->installEventFilter(this);
    }

    refreshLoginAvatarForUsernameField();
}

bool LoginWindow::cycleLoginFieldFocusWithArrow(QLineEdit* current, int key)
{
    QLineEdit* const chain[] = { m_usernameEdit, m_passwordEdit };
    constexpr int n = 2;
    int idx = -1;
    for (int i = 0; i < n; ++i) {
        if (chain[i] == current) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return false;
    const int delta = (key == Qt::Key_Down) ? 1 : -1;
    const int next = (idx + delta + n) % n;
    chain[next]->setFocus(Qt::ShortcutFocusReason);
    return true;
}

bool LoginWindow::cycleRegisterFieldFocusWithArrow(QLineEdit* current, int key)
{
    QLineEdit* const chain[] = { m_registerUsernameEdit, m_registerPasswordEdit, m_registerConfirmEdit };
    constexpr int n = 3;
    int idx = -1;
    for (int i = 0; i < n; ++i) {
        if (chain[i] == current) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return false;
    const int delta = (key == Qt::Key_Down) ? 1 : -1;
    const int next = (idx + delta + n) % n;
    chain[next]->setFocus(Qt::ShortcutFocusReason);
    return true;
}

void LoginWindow::applyCtrlHToFocusedPasswordField(QLineEdit* field)
{
    if (field == m_passwordEdit) {
        const bool toPlain = (m_passwordEdit->echoMode() == QLineEdit::Password);
        m_passwordEdit->setEchoMode(toPlain ? QLineEdit::Normal : QLineEdit::Password);
        QSignalBlocker b(m_passwordVisibleCheck);
        m_passwordVisibleCheck->setChecked(m_passwordEdit->echoMode() == QLineEdit::Normal);
        return;
    }
    if (field == m_registerPasswordEdit) {
        const bool toPlain = (m_registerPasswordEdit->echoMode() == QLineEdit::Password);
        m_registerPasswordEdit->setEchoMode(toPlain ? QLineEdit::Normal : QLineEdit::Password);
        QSignalBlocker b(m_registerPasswordVisibleCheck);
        m_registerPasswordVisibleCheck->setChecked(m_registerPasswordEdit->echoMode() == QLineEdit::Normal);
        return;
    }
    if (field == m_registerConfirmEdit) {
        const bool toPlain = (m_registerConfirmEdit->echoMode() == QLineEdit::Password);
        m_registerConfirmEdit->setEchoMode(toPlain ? QLineEdit::Normal : QLineEdit::Password);
    }
}

bool LoginWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        auto* le = qobject_cast<QLineEdit*>(watched);
        if (le && ke->modifiers() == Qt::NoModifier) {
            if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
                if (m_loginForm->isVisible() && cycleLoginFieldFocusWithArrow(le, ke->key()))
                    return true;
                if (m_registerForm->isVisible() && cycleRegisterFieldFocusWithArrow(le, ke->key()))
                    return true;
            }
        }
        if (le && ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_H) {
            if (le == m_passwordEdit || le == m_registerPasswordEdit || le == m_registerConfirmEdit) {
                applyCtrlHToFocusedPasswordField(le);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void LoginWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient grad(0, 0, qreal(width()), qreal(height()));
    grad.setColorAt(0, QColor(0xfd, 0xde, 0xbd));
    grad.setColorAt(1, QColor(0xab, 0xd8, 0xdf));
    QPainterPath path;
    path.addRoundedRect(rect(), kLoginWindowCornerRadius, kLoginWindowCornerRadius);
    p.fillPath(path, grad);
}

void LoginWindow::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateRoundedWindowMask();
}

void LoginWindow::updateRoundedWindowMask()
{
    const QSize sz = size();
    if (sz.isEmpty())
        return;
    QBitmap bm(sz);
    bm.fill(Qt::color0);
    QPainter painter(&bm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::color1);
    painter.drawRoundedRect(QRect(QPoint(0, 0), sz), kLoginWindowCornerRadius, kLoginWindowCornerRadius);
    setMask(bm);
}

void LoginWindow::buildLoginForm()
{
    QVBoxLayout* layout = new QVBoxLayout(m_loginForm);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    m_usernameEdit = new QLineEdit(m_loginForm);
    m_usernameEdit->setPlaceholderText("用户名");
    m_usernameEdit->setObjectName("loginInput");
    m_usernameEdit->setMinimumHeight(40);
    layout->addWidget(m_usernameEdit);

    QHBoxLayout* pwdLayout = new QHBoxLayout();
    pwdLayout->setSpacing(8);
    m_passwordEdit = new QLineEdit(m_loginForm);
    m_passwordEdit->setPlaceholderText("密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setObjectName("loginInput");
    m_passwordEdit->setMinimumHeight(40);
    pwdLayout->addWidget(m_passwordEdit);

    m_passwordVisibleCheck = new QCheckBox("显示", m_loginForm);
    m_passwordVisibleCheck->setObjectName("passwordVisibleCheck");
    pwdLayout->addWidget(m_passwordVisibleCheck);
    layout->addLayout(pwdLayout);

    m_loginBtn = new QPushButton("立即登录", m_loginForm);
    m_loginBtn->setObjectName("primaryButton");
    m_loginBtn->setMinimumHeight(40);
    layout->addWidget(m_loginBtn);

    QHBoxLayout* linkLayout = new QHBoxLayout();
    linkLayout->setSpacing(16);
    m_switchToRegisterBtn = new QPushButton("注册账号", m_loginForm);
    m_switchToRegisterBtn->setObjectName("textButton");
    m_switchToRegisterBtn->setCursor(Qt::PointingHandCursor);
    linkLayout->addWidget(m_switchToRegisterBtn);
    linkLayout->addStretch();
    layout->addLayout(linkLayout);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
    connect(m_switchToRegisterBtn, &QPushButton::clicked, this, &LoginWindow::onSwitchToRegister);
    connect(m_passwordVisibleCheck, &QCheckBox::toggled, this, &LoginWindow::onPasswordVisibleToggled);
}

void LoginWindow::buildRegisterForm()
{
    QVBoxLayout* layout = new QVBoxLayout(m_registerForm);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    m_registerUsernameEdit = new QLineEdit(m_registerForm);
    m_registerUsernameEdit->setPlaceholderText("用户名（2-18 字符）");
    m_registerUsernameEdit->setObjectName("loginInput");
    m_registerUsernameEdit->setMinimumHeight(40);
    layout->addWidget(m_registerUsernameEdit);

    QHBoxLayout* regPwdLayout = new QHBoxLayout();
    regPwdLayout->setSpacing(8);
    m_registerPasswordEdit = new QLineEdit(m_registerForm);
    m_registerPasswordEdit->setPlaceholderText("密码（至少 6 位）");
    m_registerPasswordEdit->setEchoMode(QLineEdit::Password);
    m_registerPasswordEdit->setObjectName("loginInput");
    m_registerPasswordEdit->setMinimumHeight(40);
    regPwdLayout->addWidget(m_registerPasswordEdit);
    m_registerPasswordVisibleCheck = new QCheckBox("显示", m_registerForm);
    m_registerPasswordVisibleCheck->setObjectName("passwordVisibleCheck");
    regPwdLayout->addWidget(m_registerPasswordVisibleCheck);
    layout->addLayout(regPwdLayout);

    m_registerConfirmEdit = new QLineEdit(m_registerForm);
    m_registerConfirmEdit->setPlaceholderText("确认密码");
    m_registerConfirmEdit->setEchoMode(QLineEdit::Password);
    m_registerConfirmEdit->setObjectName("loginInput");
    m_registerConfirmEdit->setMinimumHeight(40);
    layout->addWidget(m_registerConfirmEdit);

    m_registerBtn = new QPushButton("注册", m_registerForm);
    m_registerBtn->setObjectName("primaryButton");
    m_registerBtn->setMinimumHeight(40);
    layout->addWidget(m_registerBtn);

    m_switchToLoginBtn = new QPushButton("返回登录", m_registerForm);
    m_switchToLoginBtn->setObjectName("textButton");
    m_switchToLoginBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_switchToLoginBtn, 0, Qt::AlignCenter);

    connect(m_registerBtn, &QPushButton::clicked, this, &LoginWindow::onRegisterClicked);
    connect(m_switchToLoginBtn, &QPushButton::clicked, this, &LoginWindow::onSwitchToLogin);
    connect(m_registerPasswordVisibleCheck, &QCheckBox::toggled, this, &LoginWindow::onRegisterPasswordVisibleToggled);
}

void LoginWindow::setupStyles()
{
    setStyleSheet(ApplyStyle::loginWindowStyle());
}

void LoginWindow::showLoginForm()
{
    m_loginForm->setVisible(true);
    m_registerForm->setVisible(false);
    m_usernameEdit->clear();
    m_passwordEdit->clear();
    showError(QString());
    updateWelcomeTitle();
    refreshLoginAvatarForUsernameField();
}

void LoginWindow::showRegisterForm()
{
    m_loginForm->setVisible(false);
    m_registerForm->setVisible(true);
    m_registerUsernameEdit->clear();
    m_registerPasswordEdit->clear();
    m_registerConfirmEdit->clear();
    showError(QString());
}

void LoginWindow::showError(const QString& msg)
{
    if (msg.isEmpty()) {
        m_errorLabel->setVisible(false);
    } else {
        m_errorLabel->setStyleSheet("");
        m_errorLabel->setText(msg);
        m_errorLabel->setVisible(true);
    }
}

void LoginWindow::refreshLoginAvatarForUsernameField()
{
    if (!m_avatarLabel)
        return;
    const QString typed = m_usernameEdit->text().trimmed();
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    const QString lastLogin = settings.value(QStringLiteral("auth/lastLoginUsername")).toString();

    const qreal dpr = devicePixelRatioF();
    QPixmap pm;

    if (!typed.isEmpty() && typed == lastLogin) {
        UserDao dao;
        if (auto u = dao.findByUsername(typed)) {
            if (!u->avatarPath.isEmpty()) {
                const QString abs = UserDao::absolutePathFromProjectRelative(u->avatarPath);
                if (QFile::exists(abs)) {
                    QImage img(abs);
                    if (!img.isNull()) {
                        pm = QPixmap::fromImage(
                            img.scaled(QSize(kAvatarSide, kAvatarSide) * dpr, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
                        pm.setDevicePixelRatio(dpr);
                    }
                }
            }
        }
    }

    if (pm.isNull()) {
        QPixmap canvas(QSize(kAvatarSide, kAvatarSide) * dpr);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::transparent);
        QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
        QPainter painter(&canvas);
        renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
        pm = canvas;
    }
    m_avatarLabel->setPixmap(pm);
}

void LoginWindow::updateWelcomeTitle()
{
    UserDao dao;
    auto lastUsername = dao.getLastRegisterUsername();

    int hour = QDateTime::currentDateTime().time().hour();
    QString timeGreeting;
    if (hour >= 6 && hour < 12) {
        timeGreeting = QStringLiteral("上午好");
    } else if (hour >= 12 && hour < 18) {
        timeGreeting = QStringLiteral("下午好");
    } else {
        timeGreeting = QStringLiteral("晚上好");
    }

    if (lastUsername) {
        m_titleLabel->setText(QString("%1，%2，欢迎登录").arg(*lastUsername, timeGreeting));
    } else {
        m_titleLabel->setText(QStringLiteral("欢迎登录"));
    }
}

void LoginWindow::onLoginClicked()
{
    m_auth->login(m_usernameEdit->text(), m_passwordEdit->text());
}

void LoginWindow::onRegisterClicked()
{
    QString username = m_registerUsernameEdit->text().trimmed();
    QString password = m_registerPasswordEdit->text();
    QString confirm = m_registerConfirmEdit->text();

    if (password != confirm) {
        showError("两次输入的密码不一致");
        return;
    }

    m_auth->registerUser(username, password);
}

void LoginWindow::onSwitchToRegister()
{
    showRegisterForm();
    qDebug() << "已切换到注册界面";
}

void LoginWindow::onSwitchToLogin()
{
    showLoginForm();
    qDebug() << "已切换到登录界面";
}

void LoginWindow::onPasswordVisibleToggled(bool checked)
{
    m_passwordEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
}

void LoginWindow::onRegisterPasswordVisibleToggled(bool checked)
{
    m_registerPasswordEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);

}
