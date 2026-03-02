#include "loginwindow.h"
#include "mainwindow.h"
#include "../core/authmanager.h"
#include "../utils/applystyle.h"
#include "../data/userdao.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QSpacerItem>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QDateTime>

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
        showError(QString());
        showLoginForm();
    });
    connect(m_auth, &AuthManager::registerFailed, this, &LoginWindow::showError);
}

LoginWindow::~LoginWindow()
{
}

void LoginWindow::setupUI()
{
    setWindowTitle("AI客服 - 登录");
    setFixedSize(440, 520);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_card = new QFrame(this);
    m_card->setObjectName("loginCard");
    m_card->setFixedWidth(350);
    m_card->setMinimumHeight(400);

    QVBoxLayout* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(32, 40, 32, 40);
    cardLayout->setSpacing(16);

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

    cardLayout->addWidget(m_loginForm);
    cardLayout->addWidget(m_registerForm);
    m_registerForm->setVisible(false);

    cardLayout->addStretch();

    mainLayout->addStretch();
    updateWelcomeTitle();
    mainLayout->addWidget(m_card, 0, Qt::AlignCenter);
    mainLayout->addStretch();
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
        m_errorLabel->setText(msg);
        m_errorLabel->setVisible(true);
    }
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
