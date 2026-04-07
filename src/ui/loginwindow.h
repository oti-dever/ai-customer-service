#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

class QEvent;
class QPaintEvent;
class QResizeEvent;

class AuthManager;

class LoginWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget* parent = nullptr);
    ~LoginWindow();

    QString loggedInUsername() const { return m_loggedInUsername; }

signals:
    void loginSucceeded(const QString& username);

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onSwitchToRegister();
    void onSwitchToLogin();
    void onPasswordVisibleToggled(bool checked);
    void onRegisterPasswordVisibleToggled(bool checked);

private:
    void setupUI();
    void setupStyles();
    void buildLoginForm();
    void buildRegisterForm();
    void showLoginForm();
    void showRegisterForm();
    void showError(const QString& msg);
    void updateWelcomeTitle();
    void refreshLoginAvatarForUsernameField();
    void updateRoundedWindowMask();
    bool cycleLoginFieldFocusWithArrow(QLineEdit* current, int key);
    bool cycleRegisterFieldFocusWithArrow(QLineEdit* current, int key);
    void applyCtrlHToFocusedPasswordField(QLineEdit* field);

    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    AuthManager* m_auth = nullptr;
    QLineEdit* m_usernameEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLineEdit* m_registerUsernameEdit = nullptr;
    QLineEdit* m_registerPasswordEdit = nullptr;
    QLineEdit* m_registerConfirmEdit = nullptr;
    QPushButton* m_loginBtn = nullptr;
    QPushButton* m_registerBtn = nullptr;
    QPushButton* m_switchToRegisterBtn = nullptr;
    QPushButton* m_switchToLoginBtn = nullptr;
    QCheckBox* m_passwordVisibleCheck = nullptr;
    QCheckBox* m_registerPasswordVisibleCheck = nullptr;
    QWidget* m_loginForm = nullptr;
    QWidget* m_registerForm = nullptr;
    QWidget* m_card = nullptr;
    QLabel* m_errorLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_avatarLabel = nullptr;
    QString m_loggedInUsername;
};

#endif // LOGINWINDOW_H
