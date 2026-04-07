#include "mainwindow.h"
#include "addwindowdialog.h"
#include "aggregatechatform.h"
#include "editprofiledialog.h"
#include "foldarrowcombobox.h"
#include "rpamanagedialog.h"
#include "helpcenterdialog.h"
#include "rpa_console_window.h"
#include "../data/userdao.h"
#include "../utils/applystyle.h"
#include "../utils/swordcursor.h"
#include "../utils/win32windowhelper.h"
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QProcess>
#include <QDir>
#include <QToolButton>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSizePolicy>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QStatusBar>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QStyle>
#include <QStyleFactory>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QDirIterator>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <functional>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QImage>
#include <QSvgRenderer>

namespace {

class ProfileBarWidget final : public QWidget
{
public:
    explicit ProfileBarWidget(std::function<void()> onTap, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_onTap(std::move(onTap))
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_onTap)
            m_onTap();
        QWidget::mousePressEvent(e);
    }

private:
    std::function<void()> m_onTap;
};

} // namespace

static QPixmap roundedSidebarAvatarPixmap(const QPixmap& source, int logicalSide, qreal dpr, int cornerRadiusLogical)
{
    if (source.isNull())
        return source;
    const int s = qMax(1, qRound(logicalSide * dpr));
    const int r = qBound(1, qRound(cornerRadiusLogical * dpr), s / 2);
    QPixmap square(s, s);
    square.fill(Qt::transparent);
    {
        QPainter pt(&square);
        pt.setRenderHint(QPainter::Antialiasing);
        QPixmap fill = source.scaled(s, s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int ox = qMax(0, (fill.width() - s) / 2);
        const int oy = qMax(0, (fill.height() - s) / 2);
        pt.drawPixmap(0, 0, fill, ox, oy, s, s);
    }
    QPixmap out(s, s);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, s, s, qreal(r), qreal(r));
    p.setClipPath(path);
    p.drawPixmap(0, 0, square);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}

/// 自动扫描强度（与 QSettings quickLaunch/scanMode 对应：0..2）
enum class QuickLaunchScanMode : int {
    Strict = 0, ///< 开始菜单：目录+exe+常用关键字；不含桌面放宽
    Normal = 1, ///< 在严格基础上，桌面根目录 .lnk 只要过目录+exe 即收录
    Loose = 2,  ///< 仅目录黑名单 + exe 黑名单，不做常用关键字过滤
};

namespace {

QString stripAnsiEscapes(const QString& s)
{
    QString t = s;
    // CSI：ESC [ … 最终字节在 @–~
    static const QRegularExpression csi(QStringLiteral(R"(\x1B\[[0-?]*[ -/]*[@-~])"));
    t.replace(csi, QString());
    // OSC：ESC ] … BEL
    static const QRegularExpression osc(QStringLiteral(R"(\x1B\][^\x07]*\x07)"));
    t.replace(osc, QString());
    // 部分终端用 ST：ESC \ 结束 OSC
    static const QRegularExpression oscSt(QStringLiteral(R"(\x1B\][^\x1B]*\x1B\\)"));
    t.replace(oscSt, QString());
    // 规范化换行，避免输出里残留 '\r' 导致显示异常
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QChar('\r'), QStringLiteral(""));
    return t;
}

} // namespace

namespace {

class QuickLaunchPathReadOnlyDelegate : public QStyledItemDelegate
{
public:
    explicit QuickLaunchPathReadOnlyDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }
    QWidget* createEditor(QWidget*, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return nullptr;
    }
};

static const int kQlKindRole = Qt::UserRole;
static const int kQlPathRole = Qt::UserRole + 1;
static const QString kQlKindGroup = QStringLiteral("group");
static const QString kQlKindApp = QStringLiteral("app");

static QString quickLaunchExeNameForRunningCheck(const QString& path)
{
    const QFileInfo fi(path);
    const QString abs = fi.absoluteFilePath();
#ifdef Q_OS_WIN
    if (abs.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
        const QString target = Win32WindowHelper::resolveShortcutTarget(abs);
        if (!target.isEmpty())
            return QFileInfo(target).fileName();
        return {};
    }
#endif
    return fi.fileName();
}

static bool launchQuickLaunchPath(const QString& path, QString* errOut)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        if (errOut)
            *errOut = QStringLiteral("文件不存在");
        return false;
    }
    const QString abs = fi.absoluteFilePath();
    if (abs.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(abs))) {
            if (errOut)
                *errOut = QStringLiteral("无法打开快捷方式");
            return false;
        }
        return true;
    }
    if (!QProcess::startDetached(abs, QStringList(), fi.absolutePath())) {
        if (errOut)
            *errOut = QStringLiteral("启动失败");
        return false;
    }
    return true;
}

} // namespace

namespace {

static bool quickLaunchPathsEquivalentForDedup(const QString& a, const QString& b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    const QString aa = QDir::cleanPath(QFileInfo(a).absoluteFilePath());
    const QString bb = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
    return aa.compare(bb, Qt::CaseInsensitive) == 0;
}

static QString quickLaunchResolvedTargetForDedup(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QString abs = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_WIN
    if (abs.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
        const QString target = Win32WindowHelper::resolveShortcutTarget(abs);
        if (!target.isEmpty())
            return QDir::cleanPath(QFileInfo(target).absoluteFilePath());
        return {};
    }
#endif
    if (!QFileInfo(abs).exists())
        return {};
    return QDir::cleanPath(abs);
}

static QString quickLaunchGroupFromShortcutLocation(const QString& rootDir, const QString& shortcutAbsPath)
{
    const QString root = QDir::cleanPath(QFileInfo(rootDir).absoluteFilePath());
    const QString file = QDir::cleanPath(QFileInfo(shortcutAbsPath).absoluteFilePath());
    if (!file.startsWith(root, Qt::CaseInsensitive))
        return QStringLiteral("默认");

    QString rel = file.mid(root.size());
    if (rel.startsWith(QChar('/')) || rel.startsWith(QChar('\\')))
        rel.remove(0, 1);
    const int lastSlash = qMax(rel.lastIndexOf('/'), rel.lastIndexOf('\\'));
    if (lastSlash <= 0)
        return QStringLiteral("默认");
    const QString folder = rel.left(lastSlash);
    const QStringList parts = folder.split(QRegularExpression(QStringLiteral(R"([/\\]+)")),
                                           Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QStringLiteral("默认");
    const QString g = parts.first().trimmed();
    return g.isEmpty() ? QStringLiteral("默认") : g;
}

static QStringList quickLaunchRelPathPartsUnderRoot(const QString& rootDir, const QString& absFile)
{
    const QString rootClean = QDir::cleanPath(QFileInfo(rootDir).absoluteFilePath());
    const QString fileClean = QDir::cleanPath(QFileInfo(absFile).absoluteFilePath());
    if (fileClean.length() < rootClean.length())
        return {};
    if (!fileClean.startsWith(rootClean, Qt::CaseInsensitive))
        return {};
    QString rel = fileClean.mid(rootClean.size());
    if (rel.startsWith(QChar('/')) || rel.startsWith(QChar('\\')))
        rel.remove(0, 1);
    if (rel.isEmpty())
        return {};
    return rel.split(QRegularExpression(QStringLiteral(R"([/\\]+)")), Qt::SkipEmptyParts);
}

static bool quickLaunchScanFolderBlacklisted(const QString& folderName)
{
    static const QSet<QString> blocked = [] {
        const QStringList names = {
            QStringLiteral("administrative tools"),
            QStringLiteral("accessories"),
            QStringLiteral("system tools"),
            QStringLiteral("windows powershell"),
            QStringLiteral("windows kits"),
            QStringLiteral("accessibility"),
            QStringLiteral("speech"),
            QStringLiteral("tablet pc"),
            QStringLiteral("maintenance"),
            QStringLiteral("hyper-v"),
            QStringLiteral("windows system"),
            QStringLiteral("imaging devices"),
            QStringLiteral("windows administrative tools"),
            QStringLiteral("管理工具"),
            QStringLiteral("附件"),
            QStringLiteral("系统工具"),
            QStringLiteral("辅助功能"),
            QStringLiteral("语音识别"),
            QStringLiteral("平板电脑"),
            QStringLiteral("维护"),
        };
        QSet<QString> s;
        for (const QString& n : names)
            s.insert(n.trimmed().toLower());
        return s;
    }();
    return blocked.contains(folderName.trimmed().toLower());
}

static bool quickLaunchScanExeBlacklisted(const QString& exeBaseLower)
{
    static const QSet<QString> blocked = {
        QStringLiteral("regedit"),
        QStringLiteral("cmd"),
        QStringLiteral("powershell"),
        QStringLiteral("pwsh"),
        QStringLiteral("mmc"),
        QStringLiteral("msinfo32"),
        QStringLiteral("cleanmgr"),
        QStringLiteral("eventvwr"),
        QStringLiteral("compmgmt"),
        QStringLiteral("comexp"),
        QStringLiteral("services"),
        QStringLiteral("mstsc"),
        QStringLiteral("msdt"),
        QStringLiteral("odbcad32"),
        QStringLiteral("odbcconf"),
        QStringLiteral("gpedit"),
        QStringLiteral("diskmgmt"),
        QStringLiteral("perfmon"),
        QStringLiteral("resmon"),
        QStringLiteral("taskkill"),
        QStringLiteral("schtasks"),
        QStringLiteral("wscript"),
        QStringLiteral("cscript"),
        QStringLiteral("mshta"),
        QStringLiteral("fsquirt"),
        QStringLiteral("dfrgui"),
        QStringLiteral("certmgr"),
        QStringLiteral("certlm"),
        QStringLiteral("msconfig"),
        QStringLiteral("sigverif"),
        QStringLiteral("verifier"),
        QStringLiteral("winver"),
        QStringLiteral("cmdkey"),
        QStringLiteral("dxdiag"),
        QStringLiteral("msedgewebview2"),
        QStringLiteral("ie4uinit"),
        QStringLiteral("lpksetup"),
    };
    return blocked.contains(exeBaseLower);
}

static bool quickLaunchMatchesUserAppAllowlist(const QString& exeLower, const QString& lnkLower)
{
    static const QSet<QString> exact = {
        QStringLiteral("msedge"),
        QStringLiteral("chrome"),
        QStringLiteral("firefox"),
        QStringLiteral("vivaldi"),
        QStringLiteral("brave"),
        QStringLiteral("opera"),
        QStringLiteral("iexplore"),
        QStringLiteral("code"),
        QStringLiteral("cursor"),
        QStringLiteral("devenv"),
        QStringLiteral("windowsterminal"),
        QStringLiteral("wt"),
        QStringLiteral("wechat"),
        QStringLiteral("weixin"),
        QStringLiteral("wxwork"),
        QStringLiteral("winword"),
        QStringLiteral("excel"),
        QStringLiteral("powerpnt"),
        QStringLiteral("outlook"),
        QStringLiteral("onenote"),
        QStringLiteral("teams"),
        QStringLiteral("onedrive"),
        QStringLiteral("notepad"),
        QStringLiteral("7zfm"),
        QStringLiteral("7zg"),
        QStringLiteral("steam"),
        QStringLiteral("vlc"),
        QStringLiteral("spotify"),
        QStringLiteral("wps"),
        QStringLiteral("wpp"),
        QStringLiteral("et"),
        QStringLiteral("qq"),
        QStringLiteral("tim"),
        QStringLiteral("dingtalk"),
        QStringLiteral("feishu"),
        QStringLiteral("lark"),
        QStringLiteral("slack"),
        QStringLiteral("discord"),
        QStringLiteral("postman"),
        QStringLiteral("insomnia"),
        QStringLiteral("eclipse"),
        QStringLiteral("idea64"),
        QStringLiteral("pycharm64"),
        QStringLiteral("webstorm64"),
        QStringLiteral("goland64"),
        QStringLiteral("clion64"),
        QStringLiteral("datagrip64"),
        QStringLiteral("rider64"),
        QStringLiteral("studio64"),
    };
    if (exact.contains(exeLower) || exact.contains(lnkLower))
        return true;

    static const QStringList needles = {
        QStringLiteral("chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("msedge"),
        QStringLiteral("firefox"),
        QStringLiteral("vivaldi"),
        QStringLiteral("brave"),
        QStringLiteral("opera"),
        QStringLiteral("qqbrowser"),
        QStringLiteral("360se"),
        QStringLiteral("360chrome"),
        QStringLiteral("sogou"),
        QStringLiteral("vscode"),
        QStringLiteral("cursor"),
        QStringLiteral("devenv"),
        QStringLiteral("rider"),
        QStringLiteral("webstorm"),
        QStringLiteral("pycharm"),
        QStringLiteral("intellij"),
        QStringLiteral("goland"),
        QStringLiteral("clion"),
        QStringLiteral("datagrip"),
        QStringLiteral("android studio"),
        QStringLiteral("wechat"),
        QStringLiteral("weixin"),
        QStringLiteral("wxwork"),
        QStringLiteral("dingtalk"),
        QStringLiteral("feishu"),
        QStringLiteral("lark"),
        QStringLiteral("slack"),
        QStringLiteral("discord"),
        QStringLiteral("teams"),
        QStringLiteral("onedrive"),
        QStringLiteral("notepad++"),
        QStringLiteral("wps office"),
        QStringLiteral("kingsoft"),
        QStringLiteral("typora"),
        QStringLiteral("sublime"),
        QStringLiteral("postman"),
        QStringLiteral("insomnia"),
        QStringLiteral("eclipse"),
        QStringLiteral("steam"),
        QStringLiteral("epicgames"),
        QStringLiteral("battle.net"),
        QStringLiteral("riot"),
        QStringLiteral("spotify"),
        QStringLiteral("vlc"),
        QStringLiteral("wps"),
        QStringLiteral("qqmusic"),
        QStringLiteral("netease"),
        QStringLiteral("cloudmusic"),
    };

    auto hit = [&](const QString& hay) {
        if (hay.isEmpty())
            return false;
        for (const QString& n : needles) {
            if (hay.contains(n))
                return true;
        }
        return false;
    };
    return hit(exeLower) || hit(lnkLower);
}

/** 竖向滚动条默认隐藏；鼠标靠近视口右缘、滚轮滚动或拖条时显示，离开后延时隐藏 */
class QuickLaunchVScrollRevealHelper final : public QObject
{
public:
    explicit QuickLaunchVScrollRevealHelper(QTreeWidget* tree)
        : QObject(tree)
        , m_tree(tree)
    {
        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(420);
        QObject::connect(&m_hideTimer, &QTimer::timeout, this, [this] {
            if (!m_overScrollBar && !m_inRevealZone)
                m_tree->verticalScrollBar()->hide();
        });

        m_tree->viewport()->setMouseTracking(true);
        m_tree->viewport()->installEventFilter(this);
        m_tree->verticalScrollBar()->installEventFilter(this);

        auto scheduleSync = [this] {
            QTimer::singleShot(0, this, [this] { syncBarVisibility(); });
        };
        QObject::connect(m_tree->model(), &QAbstractItemModel::rowsInserted, this, scheduleSync);
        QObject::connect(m_tree->model(), &QAbstractItemModel::rowsRemoved, this, scheduleSync);
        QObject::connect(m_tree->model(), &QAbstractItemModel::modelReset, this, scheduleSync);
        QObject::connect(m_tree->model(), &QAbstractItemModel::layoutChanged, this, scheduleSync);

        m_tree->verticalScrollBar()->hide();
        scheduleSync();
    }

private:
    void syncBarVisibility()
    {
        QScrollBar* sb = m_tree->verticalScrollBar();
        if (sb->maximum() <= 0) {
            sb->hide();
            m_hideTimer.stop();
            return;
        }
        if (!m_inRevealZone && !m_overScrollBar)
            sb->hide();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        QScrollBar* sb = m_tree->verticalScrollBar();

        if (watched == m_tree->viewport()) {
            switch (event->type()) {
            case QEvent::MouseMove: {
                auto* me = static_cast<QMouseEvent*>(event);
                const int vw = m_tree->viewport()->width();
                const bool nearRight = me->x() >= vw - kRevealPx;
                if (nearRight != m_inRevealZone) {
                    m_inRevealZone = nearRight;
                    if (nearRight && sb->maximum() > 0) {
                        m_hideTimer.stop();
                        sb->show();
                    } else if (!m_overScrollBar) {
                        m_hideTimer.start();
                    }
                }
                break;
            }
            case QEvent::Leave:
                m_inRevealZone = false;
                if (!m_overScrollBar)
                    m_hideTimer.start();
                break;
            case QEvent::Wheel:
                if (sb->maximum() > 0) {
                    sb->show();
                    m_hideTimer.start();
                }
                break;
            case QEvent::Resize:
                QTimer::singleShot(0, this, [this] { syncBarVisibility(); });
                break;
            default:
                break;
            }
        } else if (watched == sb) {
            if (event->type() == QEvent::Enter) {
                m_overScrollBar = true;
                m_hideTimer.stop();
            } else if (event->type() == QEvent::Leave) {
                m_overScrollBar = false;
                if (!m_inRevealZone)
                    m_hideTimer.start();
            }
        }
        return QObject::eventFilter(watched, event);
    }

    QTreeWidget* m_tree;
    QTimer m_hideTimer;
    bool m_inRevealZone = false;
    bool m_overScrollBar = false;
    static constexpr int kRevealPx = 16;
};

/** Windows 原生 QTreeView 在焦点/拖放指示器上易画系统蓝；Fusion + QSS 与锌灰主题一致 */
static void polishQuickLaunchTreeWidget(QTreeWidget* tree)
{
    if (!tree)
        return;
    tree->setAlternatingRowColors(true);
    tree->setAttribute(Qt::WA_StyledBackground, true);
    if (QWidget* vp = tree->viewport())
        vp->setAttribute(Qt::WA_StyledBackground, true);
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        tree->setStyle(fusion);
}

static QString quickLaunchPathKey(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

static QStringList quickLaunchScanRootDirs()
{
    QStringList roots;
#ifdef Q_OS_WIN
    const QString programData = qEnvironmentVariable("ProgramData");
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString publicUser = qEnvironmentVariable("PUBLIC");
    if (!programData.isEmpty())
        roots << (programData + QStringLiteral("\\Microsoft\\Windows\\Start Menu\\Programs"));
    if (!appData.isEmpty())
        roots << (appData + QStringLiteral("\\Microsoft\\Windows\\Start Menu\\Programs"));
    if (!publicUser.isEmpty())
        roots << (publicUser + QStringLiteral("\\Desktop"));
#endif
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty())
        roots << desktop;
    roots.removeDuplicates();
    return roots;
}

static QSet<QString> quickLaunchDesktopPathKeys()
{
    QSet<QString> s;
#ifdef Q_OS_WIN
    const QString publicUser = qEnvironmentVariable("PUBLIC");
    if (!publicUser.isEmpty())
        s.insert(quickLaunchPathKey(publicUser + QStringLiteral("\\Desktop")));
#endif
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty())
        s.insert(quickLaunchPathKey(desktop));
    return s;
}

static bool quickLaunchIsDesktopScanRoot(const QString& rootDir)
{
    return quickLaunchDesktopPathKeys().contains(quickLaunchPathKey(rootDir));
}

static bool quickLaunchScanPassesPathAndExe(const QString& rootDir,
                                            const QString& absLnk,
                                            const QString& resolvedTarget,
                                            bool* folderFiltered,
                                            bool* exeFiltered)
{
    *folderFiltered = false;
    *exeFiltered = false;
    const QStringList parts = quickLaunchRelPathPartsUnderRoot(rootDir, absLnk);
    if (parts.isEmpty())
        return false;
    for (int i = 0; i < parts.size() - 1; ++i) {
        if (quickLaunchScanFolderBlacklisted(parts.at(i))) {
            *folderFiltered = true;
            return false;
        }
    }
    const QString exeBase = resolvedTarget.isEmpty()
                                 ? QString()
                                 : QFileInfo(resolvedTarget).completeBaseName();
    const QString exeLower = exeBase.toLower();
    if (!exeLower.isEmpty() && quickLaunchScanExeBlacklisted(exeLower)) {
        *exeFiltered = true;
        return false;
    }
    return true;
}

static bool quickLaunchScanIncludedByRules(const QString& rootDir,
                                           const QString& absLnk,
                                           const QString& resolvedTarget,
                                           QuickLaunchScanMode mode)
{
    bool ff = false;
    bool ef = false;
    if (!quickLaunchScanPassesPathAndExe(rootDir, absLnk, resolvedTarget, &ff, &ef))
        return false;
    if (mode == QuickLaunchScanMode::Loose)
        return true;

    const QString exeBase = resolvedTarget.isEmpty()
                                 ? QString()
                                 : QFileInfo(resolvedTarget).completeBaseName();
    const QString exeLower = exeBase.toLower();
    const QString lnkLower = QFileInfo(absLnk).completeBaseName().toLower();
    if (quickLaunchMatchesUserAppAllowlist(exeLower, lnkLower))
        return true;
    if (mode == QuickLaunchScanMode::Normal && quickLaunchIsDesktopScanRoot(rootDir)) {
        const QStringList parts = quickLaunchRelPathPartsUnderRoot(rootDir, absLnk);
        if (parts.size() == 1)
            return true;
    }
    return false;
}

static bool quickLaunchScanShouldIncludeWithOverrides(const QString& rootDir,
                                                      const QString& absLnk,
                                                      const QString& resolvedTarget,
                                                      QuickLaunchScanMode mode,
                                                      const QSet<QString>& forceIncKeys,
                                                      const QSet<QString>& forceExcKeys)
{
    const QString key = quickLaunchPathKey(absLnk);
    if (forceExcKeys.contains(key))
        return false;
    if (forceIncKeys.contains(key))
        return true;
    return quickLaunchScanIncludedByRules(rootDir, absLnk, resolvedTarget, mode);
}

static void quickLaunchLoadScanRules(QSet<QString>& forceIncKeys,
                                     QSet<QString>& forceExcKeys,
                                     QuickLaunchScanMode& mode)
{
    QSettings settings;
    const int m = settings.value(QStringLiteral("quickLaunch/scanMode"), 0).toInt();
    mode = static_cast<QuickLaunchScanMode>(qBound(0, m, 2));

    forceIncKeys.clear();
    {
        const int n = settings.beginReadArray(QStringLiteral("quickLaunch/scanForceInclude"));
        for (int i = 0; i < n; ++i) {
            settings.setArrayIndex(i);
            const QString p = settings.value(QStringLiteral("path")).toString();
            if (!p.isEmpty())
                forceIncKeys.insert(quickLaunchPathKey(p));
        }
        settings.endArray();
    }
    forceExcKeys.clear();
    {
        const int n = settings.beginReadArray(QStringLiteral("quickLaunch/scanForceExclude"));
        for (int i = 0; i < n; ++i) {
            settings.setArrayIndex(i);
            const QString p = settings.value(QStringLiteral("path")).toString();
            if (!p.isEmpty())
                forceExcKeys.insert(quickLaunchPathKey(p));
        }
        settings.endArray();
    }
}

static void quickLaunchSaveScanRules(const QSet<QString>& forceIncKeys,
                                     const QSet<QString>& forceExcKeys,
                                     QuickLaunchScanMode mode)
{
    QSettings settings;
    settings.setValue(QStringLiteral("quickLaunch/scanMode"), static_cast<int>(mode));

    settings.beginWriteArray(QStringLiteral("quickLaunch/scanForceInclude"));
    {
        int i = 0;
        for (const QString& k : forceIncKeys) {
            settings.setArrayIndex(i++);
            settings.setValue(QStringLiteral("path"), k);
        }
    }
    settings.endArray();

    settings.beginWriteArray(QStringLiteral("quickLaunch/scanForceExclude"));
    {
        int i = 0;
        for (const QString& k : forceExcKeys) {
            settings.setArrayIndex(i++);
            settings.setValue(QStringLiteral("path"), k);
        }
    }
    settings.endArray();
}

class QuickLaunchScanRulesDialog : public QDialog
{
public:
    explicit QuickLaunchScanRulesDialog(QWidget* parent, ApplyStyle::MainWindowTheme theme)
        : QDialog(parent)
        , m_theme(theme)
    {
        setWindowTitle(QStringLiteral("扫描黑白名单与强度"));
        setObjectName(QStringLiteral("quickLaunchRulesDialog"));
        resize(920, 520);

        quickLaunchLoadScanRules(m_forceInc, m_forceExc, m_mode);

        auto* rootLay = new QVBoxLayout(this);
        rootLay->setContentsMargins(16, 16, 16, 16);
        rootLay->setSpacing(10);

        auto* topRow = new QHBoxLayout;
        topRow->addWidget(new QLabel(QStringLiteral("扫描强度："), this));
        m_modeCombo = new FoldArrowComboBox(this);
        m_modeCombo->addItem(QStringLiteral("严格（开始菜单按常用关键字；不含桌面放宽）"));
        m_modeCombo->addItem(QStringLiteral("标准（严格 + 桌面根目录 .lnk 放行）"));
        m_modeCombo->addItem(QStringLiteral("宽松（仅排除系统目录与系统工具 exe）"));
        m_modeCombo->setCurrentIndex(static_cast<int>(m_mode));
        topRow->addWidget(m_modeCombo, 1);
        rootLay->addLayout(topRow);

        auto* hint = new QLabel(
            QStringLiteral("说明：左侧为「自动扫描不会加入」的快捷方式（按原因分组）；右侧为「会自动加入」的项。"
                           "勾选后可用下方按钮在黑白名单间移动或取消强制规则。"),
            this);
        hint->setWordWrap(true);
        rootLay->addWidget(hint);

        auto* searchRow = new QHBoxLayout;
        searchRow->addWidget(new QLabel(QStringLiteral("搜索："), this));
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setObjectName(QStringLiteral("quickLaunchRulesSearchEdit"));
        m_searchEdit->setPlaceholderText(QStringLiteral("按名称或路径过滤（两侧列表同步）；清空即显示全部"));
        m_searchEdit->setClearButtonEnabled(true);
        searchRow->addWidget(m_searchEdit, 1);
        rootLay->addLayout(searchRow);

        auto* split = new QSplitter(Qt::Horizontal, this);
        m_treeExc = new QTreeWidget(this);
        m_treeExc->setObjectName(QStringLiteral("quickLaunchRulesTree"));
        m_treeExc->setColumnCount(2);
        m_treeExc->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("路径")});
        m_treeExc->header()->setStretchLastSection(true);
        m_treeInc = new QTreeWidget(this);
        m_treeInc->setObjectName(QStringLiteral("quickLaunchRulesTree"));
        m_treeInc->setColumnCount(2);
        m_treeInc->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("路径")});
        m_treeInc->header()->setStretchLastSection(true);
        polishQuickLaunchTreeWidget(m_treeExc);
        polishQuickLaunchTreeWidget(m_treeInc);

        auto wrap = [this](const QString& title, QTreeWidget* tw) {
            auto* gb = new QGroupBox(title, this);
            auto* l = new QVBoxLayout(gb);
            l->addWidget(tw);
            return gb;
        };
        split->addWidget(wrap(QStringLiteral("未收录（扫描不会自动加入）"), m_treeExc));
        split->addWidget(wrap(QStringLiteral("可收录（扫描会自动加入）"), m_treeInc));
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 1);
        rootLay->addWidget(split, 1);

        auto* row1 = new QHBoxLayout;
        auto* bToWhite = new QPushButton(QStringLiteral("左侧勾选 → 加入白名单"), this);
        bToWhite->setObjectName(QStringLiteral("quickLaunchRulesActionButton"));
        auto* bRmBlack = new QPushButton(QStringLiteral("左侧勾选 → 移出用户黑名单"), this);
        bRmBlack->setObjectName(QStringLiteral("quickLaunchRulesActionButton"));
        row1->addWidget(bToWhite);
        row1->addWidget(bRmBlack);
        row1->addStretch(1);
        rootLay->addLayout(row1);

        auto* row2 = new QHBoxLayout;
        auto* bToBlack = new QPushButton(QStringLiteral("右侧勾选 → 加入黑名单"), this);
        bToBlack->setObjectName(QStringLiteral("quickLaunchRulesActionButton"));
        auto* bRmWhite = new QPushButton(QStringLiteral("右侧勾选 → 移出强白名单"), this);
        bRmWhite->setObjectName(QStringLiteral("quickLaunchRulesActionButton"));
        row2->addWidget(bToBlack);
        row2->addWidget(bRmWhite);
        row2->addStretch(1);
        rootLay->addLayout(row2);

        auto* row3 = new QHBoxLayout;
        row3->addStretch(1);
        auto* ok = new QPushButton(QStringLiteral("确定"), this);
        ok->setObjectName(QStringLiteral("quickLaunchOkButton"));
        auto* cancel = new QPushButton(QStringLiteral("取消"), this);
        cancel->setObjectName(QStringLiteral("quickLaunchCancelButton"));
        row3->addWidget(ok);
        row3->addWidget(cancel);
        rootLay->addLayout(row3);

        connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            m_mode = static_cast<QuickLaunchScanMode>(qBound(0, idx, 2));
            rebuildTrees();
        });
        connect(bToWhite, &QPushButton::clicked, this, [this] { moveCheckedToWhitelist(); });
        connect(bRmBlack, &QPushButton::clicked, this, [this] { removeUserBlacklistForChecked(); });
        connect(bToBlack, &QPushButton::clicked, this, [this] { moveCheckedToBlacklist(); });
        connect(bRmWhite, &QPushButton::clicked, this, [this] { removeUserWhitelistForChecked(); });
        connect(ok, &QPushButton::clicked, this, [this] {
            m_mode = static_cast<QuickLaunchScanMode>(qBound(0, m_modeCombo->currentIndex(), 2));
            quickLaunchSaveScanRules(m_forceInc, m_forceExc, m_mode);
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { applyRulesTreeSearchFilter(); });

        setStyleSheet(ApplyStyle::quickLaunchManagerStyle(theme));
        rebuildTrees();
        new QuickLaunchVScrollRevealHelper(m_treeExc);
        new QuickLaunchVScrollRevealHelper(m_treeInc);
    }

private:
    static constexpr int kAbsPathRole = Qt::UserRole + 48;

    void applyRulesTreeSearchFilter()
    {
        const QString needle = m_searchEdit ? m_searchEdit->text().trimmed() : QString();

        auto filterTree = [&](QTreeWidget* tree) {
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem* g = tree->topLevelItem(i);
                int visibleChildren = 0;
                for (int j = 0; j < g->childCount(); ++j) {
                    QTreeWidgetItem* leaf = g->child(j);
                    if (needle.isEmpty()) {
                        leaf->setHidden(false);
                        ++visibleChildren;
                    } else {
                        const bool match = leaf->text(0).contains(needle, Qt::CaseInsensitive)
                            || leaf->text(1).contains(needle, Qt::CaseInsensitive);
                        leaf->setHidden(!match);
                        if (match)
                            ++visibleChildren;
                    }
                }
                g->setHidden(visibleChildren == 0);
                if (!needle.isEmpty() && visibleChildren > 0)
                    g->setExpanded(true);
            }
        };

        filterTree(m_treeExc);
        filterTree(m_treeInc);

        if (needle.isEmpty()) {
            m_treeExc->collapseAll();
            m_treeInc->collapseAll();
        }
    }

    void rebuildTrees()
    {
        m_mode = static_cast<QuickLaunchScanMode>(qBound(0, m_modeCombo->currentIndex(), 2));
        m_treeExc->clear();
        m_treeInc->clear();

        auto makeGroup = [](QTreeWidget* tree, const QString& title) {
            auto* g = new QTreeWidgetItem(tree);
            g->setText(0, title);
            g->setFlags(g->flags() & ~Qt::ItemIsUserCheckable);
            return g;
        };

        auto* exUser = makeGroup(m_treeExc, QStringLiteral("用户黑名单"));
        auto* exFolder = makeGroup(m_treeExc, QStringLiteral("目录过滤"));
        auto* exExe = makeGroup(m_treeExc, QStringLiteral("系统程序（exe 黑名单）"));
        auto* exRule = makeGroup(m_treeExc, QStringLiteral("规则过滤（未匹配常用关键字等）"));

        auto* inUser = makeGroup(m_treeInc, QStringLiteral("用户白名单"));
        auto* inRule = makeGroup(m_treeInc, QStringLiteral("规则匹配"));

        auto addLeaf = [](QTreeWidgetItem* grp, const QString& disp, const QString& absPath) {
            auto* leaf = new QTreeWidgetItem(grp);
            leaf->setText(0, disp);
            leaf->setText(1, absPath);
            leaf->setData(0, kAbsPathRole, absPath);
            leaf->setFlags((leaf->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled)
                           & ~Qt::ItemIsAutoTristate);
            leaf->setCheckState(0, Qt::Unchecked);
        };

        const QStringList roots = quickLaunchScanRootDirs();
        QSet<QString> seenLnkKeys;
        for (const QString& root : roots) {
            if (!QDir(root).exists())
                continue;
            QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString absLnk = QDir::cleanPath(QFileInfo(it.next()).absoluteFilePath());
                if (absLnk.isEmpty())
                    continue;
                const QString key = quickLaunchPathKey(absLnk);
                if (seenLnkKeys.contains(key))
                    continue;
                seenLnkKeys.insert(key);
                const QString target = quickLaunchResolvedTargetForDedup(absLnk);
                const bool fInc = m_forceInc.contains(key);
                const bool fExc = m_forceExc.contains(key);
                const bool ruleInc = quickLaunchScanIncludedByRules(root, absLnk, target, m_mode);
                const bool effective = !fExc && (fInc || ruleInc);
                const QString disp = QFileInfo(absLnk).completeBaseName();

                if (effective) {
                    addLeaf(fInc ? inUser : inRule, disp, absLnk);
                } else {
                    QTreeWidgetItem* grp = nullptr;
                    if (fExc)
                        grp = exUser;
                    else {
                        bool ff = false;
                        bool ef = false;
                        if (!quickLaunchScanPassesPathAndExe(root, absLnk, target, &ff, &ef))
                            grp = ff ? exFolder : exExe;
                        else
                            grp = exRule;
                    }
                    addLeaf(grp, disp, absLnk);
                }
            }
        }

        auto pruneEmpty = [](QTreeWidget* tree) {
            for (int i = tree->topLevelItemCount() - 1; i >= 0; --i) {
                if (tree->topLevelItem(i)->childCount() == 0)
                    delete tree->takeTopLevelItem(i);
            }
        };
        pruneEmpty(m_treeExc);
        pruneEmpty(m_treeInc);

        m_treeExc->collapseAll();
        m_treeInc->collapseAll();
        applyRulesTreeSearchFilter();
    }

    QList<QTreeWidgetItem*> checkedLeaves(QTreeWidget* tree) const
    {
        QList<QTreeWidgetItem*> out;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* g = tree->topLevelItem(i);
            if (g->isHidden())
                continue;
            for (int j = 0; j < g->childCount(); ++j) {
                QTreeWidgetItem* c = g->child(j);
                if (c->isHidden())
                    continue;
                if (c->checkState(0) == Qt::Checked)
                    out.append(c);
            }
        }
        return out;
    }

    void moveCheckedToWhitelist()
    {
        for (auto* it : checkedLeaves(m_treeExc)) {
            const QString p = it->data(0, kAbsPathRole).toString();
            if (p.isEmpty())
                continue;
            const QString k = quickLaunchPathKey(p);
            m_forceExc.remove(k);
            m_forceInc.insert(k);
        }
        rebuildTrees();
    }

    void removeUserBlacklistForChecked()
    {
        for (auto* it : checkedLeaves(m_treeExc)) {
            const QString p = it->data(0, kAbsPathRole).toString();
            if (p.isEmpty())
                continue;
            m_forceExc.remove(quickLaunchPathKey(p));
        }
        rebuildTrees();
    }

    void moveCheckedToBlacklist()
    {
        for (auto* it : checkedLeaves(m_treeInc)) {
            const QString p = it->data(0, kAbsPathRole).toString();
            if (p.isEmpty())
                continue;
            const QString k = quickLaunchPathKey(p);
            m_forceInc.remove(k);
            m_forceExc.insert(k);
        }
        rebuildTrees();
    }

    void removeUserWhitelistForChecked()
    {
        for (auto* it : checkedLeaves(m_treeInc)) {
            const QString p = it->data(0, kAbsPathRole).toString();
            if (p.isEmpty())
                continue;
            m_forceInc.remove(quickLaunchPathKey(p));
        }
        rebuildTrees();
    }

    QComboBox* m_modeCombo = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTreeWidget* m_treeExc = nullptr;
    QTreeWidget* m_treeInc = nullptr;
    QSet<QString> m_forceInc;
    QSet<QString> m_forceExc;
    QuickLaunchScanMode m_mode = QuickLaunchScanMode::Strict;
    ApplyStyle::MainWindowTheme m_theme;
};

} // namespace

// ==================== Batch add overlay ====================

class BatchAddOverlayWidget : public QWidget
{
public:
    explicit BatchAddOverlayWidget(QWidget* parent = nullptr)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAutoFillBackground(false);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(100, 100, 110, 55));
    }
};

// ==================== RPA region calibration overlay (WeChat / 千牛 OCR) ====================

class RpaRegionCalibrationOverlay : public QWidget
{
public:
    explicit RpaRegionCalibrationOverlay(QWidget* parent = nullptr)
        : QWidget(parent,
                  Qt::Tool
                      | Qt::FramelessWindowHint
                      | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setOnFinished(std::function<void(bool, const QRect&)> cb) { m_onFinished = std::move(cb); }
    void setHelpTip(const QString& s) { m_helpTip = s; }
    void setDimColor(const QColor& c) { m_dimColor = c; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), m_dimColor);

        // help text
        p.setPen(Qt::white);
        p.setFont(QFont(p.font().family(), 10));
        p.drawText(QRect(0, 8, width(), 44), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_helpTip);

        if (m_selecting || m_selection.isValid()) {
            QRect r = m_selection.normalized();
            p.setPen(QPen(QColor(70, 170, 255, 220), 2));
            p.setBrush(QColor(70, 170, 255, 40));
            p.drawRoundedRect(r, 6, 6);

            p.setPen(Qt::white);
            p.drawText(r.adjusted(8, 8, -8, -8),
                       Qt::AlignLeft | Qt::AlignTop,
                       QStringLiteral("%1x%2").arg(r.width()).arg(r.height()));
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_selecting = true;
        m_origin = e->pos();
        m_selection = QRect(m_origin, m_origin);
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_selecting) return;
        m_selection = QRect(m_origin, e->pos());
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_selecting = false;
        m_selection = QRect(m_origin, e->pos());
        update();
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) {
            finish(false);
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finish(true);
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    void finish(bool ok)
    {
        const QRect selection = m_selection.normalized();
        auto callback = m_onFinished;
        m_onFinished = {};
        hide();
        close();
        if (m_onFinished) {
            m_onFinished(ok, selection);
        } else if (callback) {
            callback(ok, selection);
        }
        deleteLater();
    }

    bool m_selecting = false;
    QPoint m_origin;
    QRect m_selection;
    std::function<void(bool, const QRect&)> m_onFinished;
    QString m_helpTip = QStringLiteral("框选微信“聊天气泡滚动区”（Esc取消，回车确认）");
    QColor m_dimColor = QColor(20, 20, 24, 120);
};

// ==================== Tree roles & delegate ====================

namespace {
enum PlatformTreeRole {
    PlatformIdRole = Qt::UserRole,
    IsGroupRole,
    DotColorRole,
    IsCustomerServiceItemRole,
    IsActivatedRole
};

QIcon resourceIcon(const QString& path, const QIcon& fallback = {})
{
    const QIcon icon(path);
    return icon.isNull() ? fallback : icon;
}

QPixmap resourcePixmap(const QString& path, const QSize& size, const QIcon& fallback = {})
{
    const QIcon icon = resourceIcon(path, fallback);
    return icon.pixmap(size);
}

QIcon customerServiceIcon(const QString& platformId)
{
    if (platformId == QLatin1String("qianniu"))
        return resourceIcon(QStringLiteral(":/qianniu_logo.svg"));
    if (platformId == QLatin1String("pinduoduo"))
        return resourceIcon(QStringLiteral(":/pinduoduo_logo.svg"));
    if (platformId == QLatin1String("douyin"))
        return resourceIcon(QStringLiteral(":/doudian_logo.svg"));
    return {};
}

const QStringList& builtinEncouragementMessages()
{
    static const QStringList messages = {
        QStringLiteral("山高万仞，只登一步"),
        QStringLiteral("悦己者自成山海"),
        QStringLiteral("珍惜当下"),
        QStringLiteral("今天也在变好"),
        QStringLiteral("先完成，再完美"),
        QStringLiteral("保持热爱，奔赴山海"),
        QStringLiteral("向前走，自有答案"),
        QStringLiteral("认真生活，自会发光")
    };

    return messages;
}

QStringList normalizedMessages(const QStringList& messages)
{
    QStringList normalized;
    for (QString message : messages) {
        message = message.trimmed();
        if (message.isEmpty() || normalized.contains(message))
            continue;
        normalized.append(message);
    }
    return normalized;
}

QString formatRect(const QRect& r)
{
    return QStringLiteral("x=%1 y=%2 w=%3 h=%4")
        .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

struct WechatCalibrationRegionSpec {
    QString id;
    QString label;
    QString helpTip;
    int minWidth = 8;
    int minHeight = 8;
};

const QVector<WechatCalibrationRegionSpec>& wechatCalibrationRegionSpecs()
{
    static const QVector<WechatCalibrationRegionSpec> specs = {
        {
            QStringLiteral("contact_header_region"),
            QStringLiteral("聊天标题区"),
            QStringLiteral("微信 OCR 校准：框选聊天区域上方的会话标题（如“邬鸿涛”），Esc 取消，回车确认"),
            24,
            12,
        },
        {
            QStringLiteral("chat_region"),
            QStringLiteral("聊天区域"),
            QStringLiteral("微信 OCR 校准：框选中间聊天气泡滚动区，Esc 取消，回车确认"),
            32,
            32,
        },
        {
            QStringLiteral("input_box"),
            QStringLiteral("聊天输入框"),
            QStringLiteral("微信 OCR 校准：框选底部输入栏区域（用于发送/回执 OCR 与点击位置），Esc 取消，回车确认"),
            80,
            24,
        },
        {
            QStringLiteral("search_box"),
            QStringLiteral("搜索框"),
            QStringLiteral("微信 OCR 校准：框选左上搜索框完整区域，Esc 取消，回车确认"),
            32,
            16,
        },
        {
            QStringLiteral("search_result_region"),
            QStringLiteral("搜索结果区域"),
            QStringLiteral("微信 OCR 校准：框选搜索结果列表区域（包含联系人/群聊候选项），Esc 取消，回车确认"),
            32,
            16,
        },
        {
            QStringLiteral("unread_scan_band"),
            QStringLiteral("未读扫描带"),
            QStringLiteral("微信校准：框选左侧会话列表中红点所在的竖向扫描带，Esc 取消，回车确认"),
            8,
            24,
        },
        {
            QStringLiteral("conversation_list_region"),
            QStringLiteral("会话列表区域"),
            QStringLiteral("微信 OCR 校准：框选左侧会话列表区域，Esc 取消，回车确认"),
            48,
            80,
        },
    };
    return specs;
}

const WechatCalibrationRegionSpec* wechatCalibrationSpecById(const QString& id)
{
    const auto& specs = wechatCalibrationRegionSpecs();
    for (const auto& spec : specs) {
        if (spec.id == id)
            return &spec;
    }
    return nullptr;
}

const WechatCalibrationRegionSpec* wechatCalibrationSpecByLabel(const QString& label)
{
    const auto& specs = wechatCalibrationRegionSpecs();
    for (const auto& spec : specs) {
        if (spec.label == label)
            return &spec;
    }
    return nullptr;
}

/// 右键 OCR 校准菜单：按进程 exe / 窗口标题判断，不用用户填写的「平台名」
bool managedWindowShowsWechatOcrCalibration(quintptr hwnd)
{
    if (!Win32WindowHelper::isWindowValid(hwnd))
        return false;
    const QString exe = Win32WindowHelper::executableBaseNameForWindow(hwnd).toLower();
    if (exe == QLatin1String("weixin.exe") || exe == QLatin1String("wechat.exe")
        || exe == QLatin1String("wechatappex.exe")) {
        return true;
    }
    const QString t = Win32WindowHelper::windowTitle(hwnd);
    return t.contains(QStringLiteral("微信"))
        || t.contains(QLatin1String("WeChat"), Qt::CaseInsensitive);
}

bool managedWindowShowsQianniuOcrCalibration(quintptr hwnd)
{
    if (!Win32WindowHelper::isWindowValid(hwnd))
        return false;
    const QString exe = Win32WindowHelper::executableBaseNameForWindow(hwnd).toLower();
    if (exe == QLatin1String("aliworkbench.exe"))
        return true;
    const QString t = Win32WindowHelper::windowTitle(hwnd);
    return t.contains(QStringLiteral("接待中心")) || t.contains(QStringLiteral("千牛工作台"))
        || t.contains(QLatin1String("-千牛"));
}

bool managedWindowShowsPddOcrCalibration(quintptr hwnd)
{
    if (!Win32WindowHelper::isWindowValid(hwnd))
        return false;
    const QString exe = Win32WindowHelper::executableBaseNameForWindow(hwnd).toLower();
    const bool isBrowser = exe.contains(QStringLiteral("chrome"))
        || exe.contains(QStringLiteral("msedge"))
        || exe.contains(QStringLiteral("edge"));
    if (!isBrowser)
        return false;
    const QString t = Win32WindowHelper::windowTitle(hwnd);
    return t.contains(QStringLiteral("拼多多"))
        || t.contains(QStringLiteral("Pinduoduo"), Qt::CaseInsensitive)
        || t.contains(QStringLiteral("商家后台"))
        || t.contains(QStringLiteral("多多商家"));
}

QStringList loadCustomEncouragementMessages()
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    return normalizedMessages(settings.value(QStringLiteral("statusBar/customMessages")).toStringList());
}

void saveCustomEncouragementMessages(const QStringList& messages)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    settings.setValue(QStringLiteral("statusBar/customMessages"), normalizedMessages(messages));
}

QStringList allEncouragementMessages(const QStringList& customMessages)
{
    QStringList messages = builtinEncouragementMessages();
    const QStringList custom = normalizedMessages(customMessages);
    for (const QString& message : custom) {
        if (!messages.contains(message))
            messages.append(message);
    }
    return messages;
}

QString randomEncouragementText(const QStringList& messages, const QString& currentText = {})
{
    if (messages.isEmpty())
        return {};
    if (messages.size() == 1)
        return messages.first();

    QString nextText;
    do {
        const int index = QRandomGenerator::global()->bounded(messages.size());
        nextText = messages.at(index);
    } while (nextText == currentText);

    return nextText;
}

bool isWechatWindow(const WindowInfo& info)
{
    return MainWindow::isWechatWindowInfo(info);
}

bool isInWindowCloseHotspot(const QRect& rect, const QPoint& cursorPos)
{
    if (!rect.isValid()) {
        return false;
    }

    const int hotspotWidth = qMin(96, qMax(56, rect.width() / 6));
    const int hotspotHeight = qMin(48, qMax(28, rect.height() / 12));
    const QRect closeHotspot(rect.right() - hotspotWidth + 1,
                             rect.top(),
                             hotspotWidth,
                             hotspotHeight);
    return closeHotspot.contains(cursorPos);
}

QIcon onlinePlatformFallbackIcon(const WindowInfo& info)
{
    const QString proc = info.processName.toLower();
    const QString title = info.platformName.toLower();

    if (proc.contains(QStringLiteral("wechat")) || title.contains(QStringLiteral("wechat"))
        || info.platformName.contains(QStringLiteral("微信"))) {
        return resourceIcon(QStringLiteral(":/wechat_logo.svg"));
    }
    if (proc.contains(QStringLiteral("msedge")) || title.contains(QStringLiteral("edge"))) {
        return resourceIcon(QStringLiteral(":/edge_logo.svg"));
    }
    if (proc.contains(QStringLiteral("chrome")) || title.contains(QStringLiteral("chrome"))) {
        return resourceIcon(QStringLiteral(":/chrome_logo.svg"));
    }
    return {};
}

class PlatformTreeDelegate : public QStyledItemDelegate
{
public:
    explicit PlatformTreeDelegate(QTreeView* tree, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tree(tree) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool isGroup = index.data(IsGroupRole).toBool();
        return {option.rect.width(), isGroup ? 44 : 50};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const ApplyStyle::MainWindowTheme theme = ApplyStyle::loadSavedMainWindowTheme();
        const PlatformTreeColors c = ApplyStyle::platformTreeColors(theme);

        const QString title = index.data(Qt::DisplayRole).toString();
        const bool isGroup = index.data(IsGroupRole).toBool();
        const bool expanded = m_tree && m_tree->isExpanded(index);
        const bool sel = (option.state & QStyle::State_Selected) != 0;
        const bool hover = (option.state & QStyle::State_MouseOver) != 0;

        QRect r = option.rect.adjusted(6, 3, -6, -3);

        if (isGroup) {
            const QColor dotColor = index.data(DotColorRole).value<QColor>();
            QColor bg = c.groupBgDefault;
            if (hover) bg = c.groupBgHover;
            if (sel) bg = c.groupBgSelected;
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 8, 8);

            const int dot = 8;
            QRect dotRect(r.left() + 12, r.center().y() - dot / 2, dot, dot);
            painter->setBrush(dotColor);
            painter->drawEllipse(dotRect);

            QFont f = option.font;
            f.setBold(true);
            painter->setFont(f);
            painter->setPen(c.groupTextColor);
            const bool hasChildren = index.model() && index.model()->rowCount(index) > 0;
            const int textRight = hasChildren ? r.right() - 28 : r.right() - 8;
            QRect textRect(r.left() + 12 + dot + 8, r.top(), textRight - (r.left() + 12 + dot + 8), r.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            if (hasChildren) {
                const QIcon expandIcon(QStringLiteral(":/fold_arrow_to_expand_icon.svg"));
                const QIcon collapseIcon(QStringLiteral(":/fold_arrow_to_collapse_icon.svg"));
                const QSize arrowSize(16, 16);
                QRect arrowRect(r.right() - 24, r.top(), 20, r.height());
                QPixmap pix = (expanded ? collapseIcon : expandIcon).pixmap(arrowSize);
                if (!pix.isNull()) {
                    QPoint pt(arrowRect.center().x() - pix.width() / 2, arrowRect.center().y() - pix.height() / 2);
                    painter->drawPixmap(pt, pix);
                }
            }
        } else {
            bool isCS = index.data(IsCustomerServiceItemRole).toBool();
            bool isActivated = index.data(IsActivatedRole).toBool();

            QColor bg;
            if (isCS && !isActivated) {
                bg = c.itemInactiveBgDefault;
                if (hover) bg = c.itemInactiveBgHover;
                if (sel) bg = c.itemInactiveBgSelected;
            } else {
                bg = c.itemBgDefault;
                if (hover) bg = c.itemBgHover;
                if (sel) bg = c.itemBgSelected;
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 10, 10);

            int xOff = r.left() + 12;

            if (isCS) {
                int dotSz = 8;
                QColor dotClr = isActivated ? c.csDotActivated : c.csDotInactive;
                QRect dotR(xOff, r.center().y() - dotSz / 2, dotSz, dotSz);
                painter->setBrush(dotClr);
                painter->drawEllipse(dotR);
                xOff += dotSz + 8;
            }

            QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            QSize iconSize(22, 22);
            QRect iconRect(xOff, r.center().y() - iconSize.height() / 2, iconSize.width(), iconSize.height());
            if (!icon.isNull()) {
                if (isCS && !isActivated) {
                    auto pix = icon.pixmap(iconSize);
                    painter->setOpacity(c.itemInactiveIconOpacity);
                    painter->drawPixmap(iconRect, pix);
                    painter->setOpacity(1.0);
                } else {
                    icon.paint(painter, iconRect, Qt::AlignCenter);
                }
            }
            xOff += iconSize.width() + 10;

            QColor textClr = (isCS && !isActivated) ? c.itemInactiveTextColor : c.itemTextColor;
            painter->setPen(textClr);
            QRect textRect(xOff, r.top(), r.right() - 8 - xOff, r.height());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            if (sel && !isCS) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(c.itemAccentBarColor);
                painter->drawRoundedRect(QRect(r.left(), r.top() + 6, 3, r.height() - 12), 1, 1);
            }
        }
        painter->restore();
    }
private:
    QTreeView* m_tree = nullptr;
};

QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

QToolButton* makeTopIconButton(QWidget* parent, const QIcon& icon, const QString& toolTip) {
    auto* button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(18, 18));
    return button;
}
}

// ==================== MainWindow ====================

MainWindow::MainWindow(const QString& username, QWidget* parent)
    : QMainWindow(parent)
    , m_username(username)
{
    {
        UserDao dao;
        if (auto u = dao.findByUsername(username))
            m_userId = u->id;
    }

    setWindowTitle(QString("AI客服 - %1").arg(username));
    setWindowIcon(resourceIcon(QStringLiteral(":/app_icon.svg"),
                               qApp->style()->standardIcon(QStyle::SP_DesktopIcon)));
    setMinimumSize(1100, 680);
    resize(1440, 840);

    {
        QSettings pinSettings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
        m_alwaysOnTop = pinSettings.value(QStringLiteral("mainWindow/alwaysOnTop"), false).toBool();
    }

    auto* root = new QWidget(this);
    root->setObjectName("root");
    setCentralWidget(root);
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(buildLeftSidebar());

    auto* right = new QWidget(root);
    right->setObjectName("rightArea");
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(buildTopBar());
    rightLayout->addWidget(buildCenterContent(), 1);
    rootLayout->addWidget(right, 1);

    setupStyles();
    buildStatusBar();
    m_windowStateTimer = new QTimer(this);
    m_windowStateTimer->setInterval(250);
    connect(m_windowStateTimer, &QTimer::timeout,
            this, &MainWindow::checkManagedWindowsState);
    m_windowStateTimer->start();
    showSystemReadyPage();
    refreshUserProfileBar();
    SwordCursor::applyIfEnabled();
}

MainWindow::~MainWindow()
{
}

// ==================== Left Sidebar ====================

QWidget* MainWindow::buildLeftSidebar()
{
    auto* left = new QWidget(this);
    left->setObjectName("leftSidebar");
    left->setMinimumWidth(200);

    auto* layout = new QVBoxLayout(left);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);

    m_userProfileBar = new ProfileBarWidget([this] { onUserProfileBarClicked(); }, left);
    m_userProfileBar->setObjectName(QStringLiteral("userProfileBar"));
    m_userProfileBar->setFixedHeight(56);
    m_userProfileBar->setToolTip(QStringLiteral("查看并编辑个人信息"));
    auto* profileLay = new QHBoxLayout(m_userProfileBar);
    profileLay->setContentsMargins(6, 4, 6, 4);
    profileLay->setSpacing(10);
    m_userProfileAvatar = new QLabel(m_userProfileBar);
    m_userProfileAvatar->setObjectName(QStringLiteral("sidebarAvatar"));
    m_userProfileAvatar->setFixedSize(40, 40);
    m_userProfileAvatar->setAlignment(Qt::AlignCenter);
    m_userProfileAvatar->setScaledContents(false);
    m_userProfileNick = new QLabel(m_userProfileBar);
    m_userProfileNick->setObjectName(QStringLiteral("userProfileNick"));
    m_userProfileNick->setWordWrap(false);
    m_userProfileNick->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    profileLay->addWidget(m_userProfileAvatar, 0, Qt::AlignVCenter);
    profileLay->addWidget(m_userProfileNick, 1, Qt::AlignVCenter);
    layout->addWidget(m_userProfileBar);

    m_platformTreeModel = new QStandardItemModel(this);

    // -- 在线平台 --
    m_onlineGroup = new QStandardItem(QStringLiteral("在线平台"));
    m_onlineGroup->setData(QStringLiteral("online"), PlatformIdRole);
    m_onlineGroup->setData(true, IsGroupRole);
    m_onlineGroup->setData(QColor(0, 200, 120), DotColorRole);
    m_onlineGroup->setFlags(m_onlineGroup->flags() & ~Qt::ItemIsDropEnabled);

    // -- 管理后台（可折叠分组） --
    m_manageGroup = new QStandardItem(QStringLiteral("管理后台"));
    m_manageGroup->setData(QStringLiteral("manage"), PlatformIdRole);
    m_manageGroup->setData(true, IsGroupRole);
    m_manageGroup->setData(QColor(24, 144, 255), DotColorRole);
    m_manageGroup->setFlags(m_manageGroup->flags() & ~Qt::ItemIsDropEnabled);

    const QIcon iconRobot = resourceIcon(QStringLiteral(":/platform_management_icon.svg"),
                                         qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    auto* itemRobot = new QStandardItem(iconRobot, QStringLiteral("机器人管理"));
    itemRobot->setData(QStringLiteral("robot"), PlatformIdRole);
    itemRobot->setData(false, IsGroupRole);
    itemRobot->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemRobot);

    const QIcon iconReception = resourceIcon(QStringLiteral(":/aggregate_reception_icons/message_icon.svg"),
                                             qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation));
    auto* itemReception = new QStandardItem(iconReception, QStringLiteral("聚合接待"));
    itemReception->setData(QStringLiteral("aggregate"), PlatformIdRole);
    itemReception->setData(false, IsGroupRole);
    itemReception->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemReception);

    // -- 客服平台 --
    m_csGroup = new QStandardItem(QStringLiteral("客服平台"));
    m_csGroup->setData(QStringLiteral("cs"), PlatformIdRole);
    m_csGroup->setData(true, IsGroupRole);
    m_csGroup->setData(QColor(160, 160, 160), DotColorRole);
    m_csGroup->setFlags(m_csGroup->flags() & ~Qt::ItemIsDropEnabled);

    struct CsItem { const char* name; const char* id; };
    CsItem csItems[] = {{"千牛", "qianniu"}, {"拼多多", "pinduoduo"}, {"抖店", "douyin"}};
    for (const auto& cs : csItems) {
        const QString platformId = QString::fromUtf8(cs.id);
        QIcon itemIcon = customerServiceIcon(platformId);
        if (itemIcon.isNull())
            itemIcon = qApp->style()->standardIcon(QStyle::SP_DialogApplyButton);
        auto* item = new QStandardItem(itemIcon, QString::fromUtf8(cs.name));
        item->setData(platformId, PlatformIdRole);
        item->setData(false, IsGroupRole);
        item->setData(true, IsCustomerServiceItemRole);
        item->setData(false, IsActivatedRole);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        m_csGroup->appendRow(item);
    }

    m_platformTreeModel->appendRow(m_onlineGroup);
    m_platformTreeModel->appendRow(m_manageGroup);
    m_platformTreeModel->appendRow(m_csGroup);

    m_platformTree = new QTreeView(left);
    m_platformTree->setObjectName("platformList");
    m_platformTree->setModel(m_platformTreeModel);
    m_platformTree->setItemDelegate(new PlatformTreeDelegate(m_platformTree, m_platformTree));
    m_platformTree->setHeaderHidden(true);
    m_platformTree->setIndentation(16);
    m_platformTree->setRootIsDecorated(false);
    m_platformTree->setExpandsOnDoubleClick(true);
    m_platformTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_platformTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_platformTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_platformTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_platformTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_platformTree->setUniformRowHeights(false);
    m_platformTree->setMouseTracking(true);
    m_platformTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_platformTree->expandAll();
    layout->addWidget(m_platformTree);

    updateTreeViewHeight();
    connect(m_platformTree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onPlatformTreeSelectionChanged);
    connect(m_platformTree, &QTreeView::clicked,
            this, &MainWindow::onPlatformTreeClicked);
    connect(m_platformTree, &QTreeView::expanded,
            this, [this](const QModelIndex&) { updateTreeViewHeight(); });
    connect(m_platformTree, &QTreeView::collapsed,
            this, [this](const QModelIndex&) { updateTreeViewHeight(); });
    connect(m_platformTree, &QTreeView::customContextMenuRequested,
            this, &MainWindow::showPlatformContextMenu);

    return left;
}

void MainWindow::refreshUserProfileBar()
{
    if (!m_userProfileAvatar || !m_userProfileNick)
        return;
    UserDao dao;
    auto u = dao.findByUsername(m_username);
    if (!u) {
        m_userProfileNick->setText(m_username);
        m_userId = 0;
        return;
    }
    m_userId = u->id;
    const QString shown = u->displayName.isEmpty() ? u->username : u->displayName;
    m_userProfileNick->setText(shown);

    const int side = 40;
    const qreal dpr = devicePixelRatioF();
    QPixmap pm;
    if (!u->avatarPath.isEmpty()) {
        const QString abs = UserDao::absolutePathFromProjectRelative(u->avatarPath);
        if (QFile::exists(abs)) {
            QImage img(abs);
            if (!img.isNull()) {
                pm = QPixmap::fromImage(
                    img.scaled(QSize(side, side) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                pm.setDevicePixelRatio(dpr);
            }
        }
    }
    if (pm.isNull()) {
        QPixmap canvas(QSize(side, side) * dpr);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::transparent);
        QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
        QPainter painter(&canvas);
        renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
        pm = canvas;
    }
    pm = roundedSidebarAvatarPixmap(pm, side, dpr, 8);
    m_userProfileAvatar->setPixmap(pm);
}

void MainWindow::onUserProfileBarClicked()
{
    UserDao dao;
    auto u = dao.findByUsername(m_username);
    if (!u) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("无法读取当前用户信息"));
        return;
    }
    EditProfileDialog dlg(*u, this, mainWindowTheme());
    if (dlg.exec() == QDialog::Accepted) {
        refreshUserProfileBar();
        if (m_aggregateChatForm)
            m_aggregateChatForm->refreshLocalUserProfile();
    }
}

// ==================== Top Bar ====================

QWidget* MainWindow::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(52);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);

    auto* logo = new QLabel(bar);
    logo->setObjectName("logo");
    logo->setFixedSize(22, 22);
    logo->setPixmap(resourcePixmap(QStringLiteral(":/app_icon.svg"), QSize(22, 22),
                                   qApp->style()->standardIcon(QStyle::SP_DesktopIcon)));

    auto* title = new QLabel(QStringLiteral("AI客服 - %1").arg(m_username), bar);
    title->setObjectName("topTitle");

    layout->addWidget(logo);
    layout->addWidget(title);
    layout->addSpacing(8);

    m_btnAdd = makeTopIconButton(bar, resourceIcon(QStringLiteral(":/add_new_window_icon.svg"),
                                                   qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder)), QStringLiteral("添加新窗口"));
    m_btnRefresh = makeTopIconButton(bar, resourceIcon(QStringLiteral(":/home_icon.svg"),
                                                       qApp->style()->standardIcon(QStyle::SP_DirHomeIcon)), QStringLiteral("返回就绪页"));
    auto* bugBtn = makeTopIconButton(bar, QIcon(QStringLiteral(":/bug_log_icon.svg")),
                                     QStringLiteral("查看 Bug 修复日志"));
    auto* helpBtn = makeTopIconButton(bar, QIcon(QStringLiteral(":/question_mark_icon.svg")),
                                      QStringLiteral("查看软件使用说明"));
    layout->addWidget(m_btnAdd);
    layout->addWidget(m_btnRefresh);

    auto* readyWrap = new QWidget(bar);
    readyWrap->setObjectName("readyWrap");
    auto* readyLayout = new QHBoxLayout(readyWrap);
    readyLayout->setContentsMargins(10, 6, 10, 6);
    readyLayout->setSpacing(6);
    auto* readyIcon = new QLabel(readyWrap);
    readyIcon->setPixmap(resourcePixmap(QStringLiteral(":/system_ready_icon.svg"), QSize(18, 18),
                                        qApp->style()->standardIcon(QStyle::SP_ArrowUp)));
    auto* readyText = new QLabel(QStringLiteral("系统就绪"), readyWrap);
    readyText->setObjectName("readyText");
    readyLayout->addWidget(readyIcon);
    readyLayout->addWidget(readyText);
    layout->addWidget(readyWrap);
    layout->addStretch(1);
    layout->addWidget(bugBtn);
    layout->addWidget(helpBtn);

    m_btnPinTop = makeTopIconButton(bar, QIcon(), QStringLiteral("置顶"));
    m_btnPinTop->setObjectName(QStringLiteral("pinTopButton"));
    updatePinTopButtonUi();
    layout->addWidget(m_btnPinTop);
    connect(m_btnPinTop, &QToolButton::clicked, this, [this] {
        applyAlwaysOnTop(!m_alwaysOnTop);
    });

    connect(m_btnAdd, &QToolButton::clicked, this, &MainWindow::openAddWindowDialog);
    connect(m_btnRefresh, &QToolButton::clicked, this, &MainWindow::showSystemReadyPage);
    connect(bugBtn, &QToolButton::clicked, this, &MainWindow::openBugLogDialog);
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::openAppHelpDialog);
    return bar;
}

// ==================== Center Content ====================

QWidget* MainWindow::buildCenterContent()
{
    m_centerStack = new QStackedWidget(this);
    m_centerStack->setObjectName("centerStack");
    m_centerStack->addWidget(buildReadyPage()); // index 0

    m_placeholderPage = new QWidget(this);
    m_placeholderPage->setObjectName("placeholderPage");
    auto* phLayout = new QVBoxLayout(m_placeholderPage);
    phLayout->setContentsMargins(0, 0, 0, 0);
    phLayout->setAlignment(Qt::AlignCenter);
    m_placeholderLabel = new QLabel(m_placeholderPage);
    m_placeholderLabel->setText("placeholderText");
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    phLayout->addWidget(m_placeholderLabel);
    m_centerStack->addWidget(m_placeholderPage); // index 1

    return m_centerStack;
}

void MainWindow::updateTreeViewHeight()
{
    if (!m_platformTree || !m_platformTreeModel) return;
    int totalHeight = 0;
    int itemCount = m_platformTreeModel->rowCount();
    for (int i = 0; i < itemCount; ++i) {
        QModelIndex index = m_platformTreeModel->index(i, 0);
        bool isGroup = index.data(IsGroupRole).toBool();
        totalHeight += isGroup ? 44 : 50;
        if (m_platformTree->isExpanded(index)) {
            int childCount = m_platformTreeModel->rowCount(index);
            totalHeight += childCount * 50;
        }
    }
    m_platformTree->setMinimumHeight(totalHeight + 20);
}

// ==================== Tree Navigation ====================

void MainWindow::onPlatformTreeSelectionChanged()
{
    QModelIndex idx = m_platformTree->currentIndex();
    if (!idx.isValid()) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showSystemReadyPage();
        return;
    }

    QString id = idx.data(PlatformIdRole).toString();
    bool isGroup = idx.data(IsGroupRole).toBool();

    if (isGroup) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showSystemReadyPage();
        return;
    }
    if (id == QLatin1String("aggregate")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        openAggregateChatForm();
        return;
    }
    if (id == QLatin1String("manage") || id == QLatin1String("robot")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showPlaceholderPage(idx.data(Qt::DisplayRole).toString());
        return;
    }

    // Customer service item — not activated yet
    bool isCS = idx.data(IsCustomerServiceItemRole).toBool();
    bool isActivated = idx.data(IsActivatedRole).toBool();
    if (isCS && !isActivated) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        QString name = idx.data(Qt::DisplayRole).toString();
        showPlaceholderPage(QStringLiteral("请通过顶部「添加新窗口」按钮关联 %1 窗口").arg(name));
        return;
    }

    // Managed window
    if (m_managedWindows.contains(id)) {
        switchToWindow(id);
        return;
    }

    hideCurrentFloatWindow();
    m_activeWindowId.clear();
    showPlaceholderPage(idx.data(Qt::DisplayRole).toString());
}

void MainWindow::onPlatformTreeClicked(const QModelIndex& idx)
{
    if (!idx.isValid()) return;
    bool isGroup = idx.data(IsGroupRole).toBool();
    if (isGroup) {
        m_platformTree->setExpanded(idx, !m_platformTree->isExpanded(idx));
    }
}

// ==================== Page Switching ====================

void MainWindow::showSystemReadyPage()
{
    m_centerStack->setCurrentIndex(0);
}

void MainWindow::showPlaceholderPage(const QString& title)
{
    m_placeholderLabel->setText(QStringLiteral("%1").arg(title));
    m_centerStack->setCurrentWidget(m_placeholderPage);
}

void MainWindow::setupStyles()
{
    m_mainWindowTheme = ApplyStyle::loadSavedMainWindowTheme();
    setStyleSheet(ApplyStyle::mainWindowStyle(m_mainWindowTheme));
}

void MainWindow::applyMainWindowTheme(ApplyStyle::MainWindowTheme theme)
{
    m_mainWindowTheme = theme;
    ApplyStyle::saveMainWindowTheme(theme);
    setStyleSheet(ApplyStyle::mainWindowStyle(theme));
    if (m_platformTree && m_platformTree->viewport())
        m_platformTree->viewport()->update();
    if (m_aggregateChatForm)
        m_aggregateChatForm->applyTheme(theme);
}

static constexpr int kOneClickMinOnline = 2;
static constexpr int kOneClickMaxOnline = 50;

QWidget* MainWindow::buildReadyPage()
{
    auto* center = new QWidget(this);
    center->setObjectName("centerArea");

    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);

    m_readyCard = makeCard(center);
    m_readyCard->setObjectName("readyCard");
    m_readyCard->setFixedWidth(560);

    auto* cardLayout = new QVBoxLayout(m_readyCard);
    cardLayout->setContentsMargins(28, 26, 28, 26);
    cardLayout->setSpacing(10);
    cardLayout->setAlignment(Qt::AlignHCenter);

    auto* rocketWrap = new QFrame(m_readyCard);
    rocketWrap->setObjectName("rocketWrap");
    rocketWrap->setFixedSize(360, 94);
    auto* rocketLayout = new QHBoxLayout(rocketWrap);
    rocketLayout->setContentsMargins(16, 16, 16, 16);
    rocketLayout->addStretch(1);
    auto* rocket = new QLabel(rocketWrap);
    rocket->setPixmap(resourcePixmap(QStringLiteral(":/rocket_icon.svg"), QSize(60, 60),
                                     qApp->style()->standardIcon(QStyle::SP_ArrowUp)));
    rocketLayout->addWidget(rocket);
    rocketLayout->addStretch(1);

    m_readyTitle = new QLabel(QStringLiteral("系统就绪"), m_readyCard);
    m_readyTitle->setObjectName("readyTitle");
    m_readyTitle->setAlignment(Qt::AlignHCenter);

    auto* divider = new QFrame(m_readyCard);
    divider->setObjectName("divider");
    divider->setFixedHeight(1);
    divider->setFixedWidth(220);

    m_readySubtitle = new QLabel(QStringLiteral("选择左侧平台管理窗口"), m_readyCard);
    m_readySubtitle->setObjectName("readySubtitle");
    m_readySubtitle->setAlignment(Qt::AlignHCenter);

    cardLayout->addWidget(rocketWrap);
    cardLayout->addWidget(m_readyTitle);
    cardLayout->addWidget(divider, 0, Qt::AlignHCenter);
    cardLayout->addWidget(m_readySubtitle);

    auto* quickRow = new QWidget(center);
    auto* quickLayout = new QHBoxLayout(quickRow);
    quickLayout->setContentsMargins(0, 18, 0, 0);
    quickLayout->setSpacing(18);
    quickLayout->setAlignment(Qt::AlignHCenter);

    auto makeQuick = [&](const QIcon& icon, const QString& text) -> QToolButton* {
        auto* btn = new QToolButton(quickRow);
        btn->setObjectName("quickCard");
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(icon);
        btn->setIconSize(QSize(26, 26));
        btn->setText(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setAutoRaise(false);
        btn->setFixedSize(150, 120);
        return btn;
    };
    auto* btnPick = makeQuick(resourceIcon(QStringLiteral(":/one_click_aggregation_icon.svg"),
                                           qApp->style()->standardIcon(QStyle::SP_ArrowRight)),
                              QStringLiteral("一键聚合"));
    m_btnOneClickAggregate = btnPick;
    updateOneClickAggregateTooltip();
    btnPick->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btnPick, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_btnOneClickAggregate);
        QAction* setLimit = menu.addAction(QStringLiteral("设置在线窗口上限..."));
        QAction* triggered = menu.exec(m_btnOneClickAggregate->mapToGlobal(pos));
        if (triggered == setLimit) {
            const int cur = oneClickMaxOnlineLimit();
            bool ok = false;
            int v = QInputDialog::getInt(this, QStringLiteral("在线窗口上限"),
                                         QStringLiteral("上限为 %1～%2，建议不超过 30 以保持流畅：")
                                             .arg(kOneClickMinOnline).arg(kOneClickMaxOnline),
                                         cur, kOneClickMinOnline, kOneClickMaxOnline, 1, &ok);
            if (ok)
                setOneClickMaxOnlineLimit(v);
        }
    });
    connect(btnPick, &QToolButton::clicked, this, &MainWindow::startOneClickAggregate);
    auto* btnEmbed = makeQuick(resourceIcon(QStringLiteral(":/start_or_stop_rpa_icon.svg"),
                                            qApp->style()->standardIcon(QStyle::SP_FileDialogListView)),
                               QStringLiteral("管理启动/停止RPA"));
    m_btnRpaManage = btnEmbed;
    m_btnRpaManage->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_btnRpaManage, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_btnRpaManage);
        QAction* viewConsole = menu.addAction(QStringLiteral("查看控制台输出"));
        QAction* triggered = menu.exec(m_btnRpaManage->mapToGlobal(pos));
        if (triggered == viewConsole)
            openRpaConsoleWindow();
    });
    m_btnRpaManage->setToolTip(QStringLiteral("左键：管理启动/停止 RPA\n右键：查看控制台输出"));
    auto* btnStart = makeQuick(resourceIcon(QStringLiteral(":/quick_launch_application_icon.svg"),
                                            qApp->style()->standardIcon(QStyle::SP_DialogOkButton)),
                               QStringLiteral("快速启动应用"));
    m_btnQuickStart = btnStart;
    m_btnQuickStart->setToolTip(
        QStringLiteral("左键：按列表快速启动（受「数量上限」约束，默认前 10 项）\n右键：管理应用列表 / 设置数量上限"));
    connect(btnEmbed, &QToolButton::clicked, this, &MainWindow::openRpaManageDialog);
    connect(btnStart, &QToolButton::clicked, this, &MainWindow::runQuickLaunchApps);
    btnStart->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btnStart, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_btnQuickStart);
        QAction* manage = menu.addAction(QStringLiteral("管理应用列表"));
        QAction* setCap = menu.addAction(QStringLiteral("设置数量上限…"));
        QAction* triggered = menu.exec(m_btnQuickStart->mapToGlobal(pos));
        if (triggered == manage) {
            openQuickLaunchManager();
        } else if (triggered == setCap) {
            QSettings settings;
            const int cur = qBound(1, settings.value(QStringLiteral("quickLaunch/maxLaunchCount"), 10).toInt(), 30);
            bool ok = false;
            const int v = QInputDialog::getInt(
                this,
                QStringLiteral("快速启动 — 数量上限"),
                QStringLiteral(
                    "单次「快速启动」只会按列表顺序处理「前 N 个条目」（序号从第 1 项起算；"
                    "路径为空的行也会占用序号）。\n\n"
                    "请勿一次启动过多应用，否则可能导致系统卡顿、内存或 CPU 占用过高、"
                    "部分软件并发异常等风险，后果自负。\n\n"
                    "请输入 N（1～30，默认建议 10）："),
                cur,
                1,
                30,
                1,
                &ok);
            if (ok) {
                settings.setValue(QStringLiteral("quickLaunch/maxLaunchCount"), v);
                statusBar()->showMessage(QStringLiteral("快速启动数量上限已设为 %1").arg(v), 5000);
            }
        }
    });
    quickLayout->addWidget(btnPick);
    quickLayout->addWidget(btnEmbed);
    quickLayout->addWidget(btnStart);
    layout->addWidget(m_readyCard, 0, Qt::AlignHCenter);
    layout->addWidget(quickRow, 0, Qt::AlignHCenter);
    layout->addStretch(2);

    return center;
}

// ==================== Add Window Dialog ====================

void MainWindow::openAddWindowDialog()
{
    AddWindowDialog dlg(this);
    dlg.exec();
}

void MainWindow::openRpaManageDialog()
{
    RpaManageDialog dlg(this, this);
    dlg.exec();
}

void MainWindow::openRpaConsoleWindow()
{
    if (!m_rpaConsoleWindow) {
        m_rpaConsoleWindow = new RpaConsoleWindow(this, this);
        connect(m_rpaConsoleWindow, &QObject::destroyed, this, [this]() {
            m_rpaConsoleWindow = nullptr;
        });
    }
    m_rpaConsoleWindow->show();
    m_rpaConsoleWindow->raise();
    m_rpaConsoleWindow->activateWindow();
}

QString MainWindow::rpaProcessLog(const QString& platformId) const
{
    return m_rpaProcessLogs.value(platformId);
}

void MainWindow::clearRpaProcessLog(const QString& platformId)
{
    if (platformId.isEmpty())
        return;
    m_rpaProcessLogs.remove(platformId);
}

void MainWindow::appendRpaProcessLog(const QString& platformId, const QString& text)
{
    if (text.isEmpty())
        return;
    constexpr int kMaxRpaLogChars = 400000;
    QString& buf = m_rpaProcessLogs[platformId];
    buf.append(text);
    if (buf.size() > kMaxRpaLogChars)
        buf.remove(0, buf.size() - kMaxRpaLogChars);
    emit rpaProcessOutputAppended(platformId, text);
}

QStringList MainWindow::runningRpaPlatformIds() const
{
    QStringList out;
    for (auto it = m_rpaProcesses.constBegin(); it != m_rpaProcesses.constEnd(); ++it) {
        QProcess* p = it.value();
        if (p && p->state() == QProcess::Running)
            out.append(it.key());
    }
    return out;
}

void MainWindow::startRpaPlatforms(const QStringList& platformIds)
{
    const QString pythonRoot = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python");
    for (const QString& id : platformIds) {
        if (id != QStringLiteral("wechat") && id != QStringLiteral("qianniu") && id != QStringLiteral("pdd"))
            continue;

        if (m_rpaProcesses.contains(id)) {
            QProcess* existing = m_rpaProcesses.value(id);
            if (existing && existing->state() == QProcess::Running)
                continue;
            if (existing) {
                m_rpaProcesses.remove(id);
                existing->disconnect();
                existing->deleteLater();
            }
        }

        auto* proc = new QProcess(this);
        proc->setProgram(QStringLiteral("python"));
        proc->setArguments(QStringList() << QStringLiteral("-m") << QStringLiteral("rpa.main")
                                         << QStringLiteral("--platform") << id);
        proc->setWorkingDirectory(pythonRoot);
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            // Windows 下保证 stdout/stderr 以 UTF-8 模式输出（避免乱码）
            env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
            env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
            proc->setProcessEnvironment(env);
        }
        m_rpaConsoleDecoders.insert(id, QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8));
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, id, proc]() {
            if (m_rpaProcesses.value(id) != proc)
                return;
            const QByteArray chunk = proc->readAllStandardOutput();
            if (chunk.isEmpty())
                return;
            auto it = m_rpaConsoleDecoders.find(id);
            if (it == m_rpaConsoleDecoders.end())
                return;
            QString decoded = it.value()->decode(chunk);
            // 如果 UTF-8 解码出现大量替换字符，说明实际输出可能是本地编码（常见 GBK/CP936）
            if (decoded.contains(QChar::ReplacementCharacter)) {
                const QString localDecoded = stripAnsiEscapes(QString::fromLocal8Bit(chunk));
                if (!localDecoded.isEmpty()) {
                    // reset 解码状态，避免后续分片继续受错误状态影响
                    it.value() = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                    appendRpaProcessLog(id, localDecoded);
                    return;
                }
            }
            const QString cleaned = stripAnsiEscapes(decoded);
            if (!cleaned.isEmpty())
                appendRpaProcessLog(id, cleaned);
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, id, proc](int exitCode, QProcess::ExitStatus status) {
                    Q_UNUSED(status)
                    if (m_rpaProcesses.value(id) != proc)
                        return;
                    const QByteArray tail = proc->readAllStandardOutput();
                    auto it = m_rpaConsoleDecoders.find(id);
                    if (it != m_rpaConsoleDecoders.end()) {
                        if (!tail.isEmpty()) {
                            QString decoded = it.value()->decode(tail);
                            if (decoded.contains(QChar::ReplacementCharacter)) {
                                const QString localDecoded = stripAnsiEscapes(QString::fromLocal8Bit(tail));
                                if (!localDecoded.isEmpty()) {
                                    it.value() = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                                    appendRpaProcessLog(id, localDecoded);
                                } else {
                                    const QString cleaned = stripAnsiEscapes(decoded);
                                    if (!cleaned.isEmpty())
                                        appendRpaProcessLog(id, cleaned);
                                }
                            } else {
                                const QString cleaned = stripAnsiEscapes(decoded);
                                if (!cleaned.isEmpty())
                                    appendRpaProcessLog(id, cleaned);
                            }
                        }
                        // flush any partial UTF-8 sequence
                        const QString flushed = stripAnsiEscapes(it.value()->decode(QByteArray()));
                        if (!flushed.isEmpty())
                            appendRpaProcessLog(id, flushed);
                    }
                    appendRpaProcessLog(id, QStringLiteral("\n[进程已退出，退出码 %1]\n").arg(exitCode));
                    m_rpaProcesses.remove(id);
                    m_rpaConsoleDecoders.remove(id);
                    proc->deleteLater();
                });
        connect(proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError e) {
            qWarning() << "[RPA] process error" << id << static_cast<int>(e);
            appendRpaProcessLog(id, QStringLiteral("\n[进程错误] code=%1\n").arg(static_cast<int>(e)));
        });

        proc->start();
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[RPA] failed to start" << id;
            appendRpaProcessLog(id, QStringLiteral("[启动失败] 无法在 PATH 中找到可用的 python，或 3 秒内未能启动。\n"));
            statusBar()->showMessage(
                QStringLiteral("启动失败：请确保已安装 Python 并在 PATH 中可用（python）。"), 5000);
            proc->deleteLater();
            continue;
        }
        appendRpaProcessLog(id, QStringLiteral("[RPA] 已启动: python -m rpa.main --platform %1\n").arg(id));
        m_rpaProcesses.insert(id, proc);
        statusBar()->showMessage(QStringLiteral("已启动 RPA：%1").arg(id), 3000);
    }
}

void MainWindow::stopRpaPlatforms(const QStringList& platformIds)
{
    for (const QString& id : platformIds) {
        QProcess* proc = m_rpaProcesses.take(id);
        if (!proc)
            continue;
        proc->disconnect();
        appendRpaProcessLog(id, QStringLiteral("\n[用户请求停止]\n"));
        proc->kill();
        proc->waitForFinished(3000);
        const QByteArray tail = proc->readAllStandardOutput();
        auto it = m_rpaConsoleDecoders.find(id);
        if (it != m_rpaConsoleDecoders.end() && !tail.isEmpty()) {
            QString decoded = it.value()->decode(tail);
            if (decoded.contains(QChar::ReplacementCharacter)) {
                const QString localDecoded = stripAnsiEscapes(QString::fromLocal8Bit(tail));
                if (!localDecoded.isEmpty()) {
                    it.value() = QSharedPointer<QStringDecoder>::create(QStringDecoder::Utf8);
                    appendRpaProcessLog(id, localDecoded);
                } else {
                    const QString cleaned = stripAnsiEscapes(decoded);
                    if (!cleaned.isEmpty())
                        appendRpaProcessLog(id, cleaned);
                }
            } else {
                const QString cleaned = stripAnsiEscapes(decoded);
                if (!cleaned.isEmpty())
                    appendRpaProcessLog(id, cleaned);
            }
            const QString flushed = stripAnsiEscapes(it.value()->decode(QByteArray()));
            if (!flushed.isEmpty())
                appendRpaProcessLog(id, flushed);
        }
        m_rpaConsoleDecoders.remove(id);
        proc->deleteLater();
        statusBar()->showMessage(QStringLiteral("已停止 RPA：%1").arg(id), 3000);
    }
}

int MainWindow::oneClickMaxOnlineLimit() const
{
    QSettings s(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    int v = s.value(QStringLiteral("oneClickAggregate/maxOnline"), 10).toInt();
    return qBound(kOneClickMinOnline, v, kOneClickMaxOnline);
}

void MainWindow::setOneClickMaxOnlineLimit(int n)
{
    int v = qBound(kOneClickMinOnline, n, kOneClickMaxOnline);
    QSettings s(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    s.setValue(QStringLiteral("oneClickAggregate/maxOnline"), v);
    updateOneClickAggregateTooltip();
}

void MainWindow::updateOneClickAggregateTooltip()
{
    if (m_btnOneClickAggregate) {
        const int limit = oneClickMaxOnlineLimit();
        m_btnOneClickAggregate->setToolTip(
            QStringLiteral("自动聚合可识别窗口（客服平台/微信/浏览器及普通应用），在线窗口上限为 %1（右键可修改）").arg(limit));
    }
}

void MainWindow::updatePinTopButtonUi()
{
    if (!m_btnPinTop)
        return;
    if (m_alwaysOnTop) {
        m_btnPinTop->setIcon(resourceIcon(QStringLiteral(":/after_pinning.svg")));
        m_btnPinTop->setToolTip(QStringLiteral("取消置顶"));
    } else {
        m_btnPinTop->setIcon(resourceIcon(QStringLiteral(":/before_pinning.svg")));
        m_btnPinTop->setToolTip(QStringLiteral("置顶"));
    }
}

void MainWindow::applyAlwaysOnTop(bool on)
{
    if (m_alwaysOnTop == on)
        return;
    m_alwaysOnTop = on;
    Win32WindowHelper::applyNativeTopMost(this, on);
    QSettings pinSettings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    pinSettings.setValue(QStringLiteral("mainWindow/alwaysOnTop"), m_alwaysOnTop);
    updatePinTopButtonUi();
}

void MainWindow::startOneClickAggregate()
{
    const QVector<WindowInfo> list = Win32WindowHelper::enumTopLevelWindows();
    QVector<WindowInfo> queue;
    queue.reserve(list.size());

    int onlineCount = 0;
    const int maxOnline = oneClickMaxOnlineLimit();

    for (const auto& info : list) {
        if (!info.handle)
            continue;
        bool alreadyManaged = false;
        for (auto it = m_managedWindows.constBegin(); it != m_managedWindows.constEnd(); ++it) {
            if (it.value().handle == info.handle) {
                alreadyManaged = true;
                break;
            }
        }
        if (alreadyManaged)
            continue;

        const QString csId = matchCustomerServicePlatform(info);
        if (!csId.isEmpty()) {
            queue.append(info);
            continue;
        }

        if (onlineCount >= maxOnline)
            continue;

        if (MainWindow::isWechatWindowInfo(info) || info.isBrowserLike) {
            queue.append(info);
            ++onlineCount;
        } else {
            queue.append(info);
            ++onlineCount;
        }
    }

    if (queue.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("未发现可聚合窗口"), 4000);
        return;
    }

    startBatchAddWindows(queue);
}

static void loadQuickLaunchConfig(QVector<QuickLaunchApp>& apps,
                                  bool& onlyIfNotRunning)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    const int size = settings.beginReadArray(QStringLiteral("quickLaunch/apps"));
    apps.clear();
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QuickLaunchApp app;
        app.name = settings.value(QStringLiteral("name")).toString();
        app.path = settings.value(QStringLiteral("path")).toString();
        app.group = settings.value(QStringLiteral("group")).toString();
        if (!app.path.isEmpty())
            apps.append(app);
    }
    settings.endArray();
    onlyIfNotRunning = settings.value(QStringLiteral("quickLaunch/onlyIfNotRunning"), true).toBool();
}

static void saveQuickLaunchConfig(const QVector<QuickLaunchApp>& apps,
                                  bool onlyIfNotRunning)
{
    QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
    settings.beginWriteArray(QStringLiteral("quickLaunch/apps"));
    for (int i = 0; i < apps.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), apps[i].name);
        settings.setValue(QStringLiteral("path"), apps[i].path);
        settings.setValue(QStringLiteral("group"), apps[i].group);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("quickLaunch/onlyIfNotRunning"), onlyIfNotRunning);
}

void MainWindow::openQuickLaunchManager()
{
    loadQuickLaunchConfig(m_quickLaunchApps, m_quickLaunchOnlyIfNotRunning);
    const ApplyStyle::MainWindowTheme theme = m_mainWindowTheme;

    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("quickLaunchManagerDialog"));
    dlg.setWindowTitle(QStringLiteral("管理应用列表"));
    dlg.resize(640, 440);

    auto* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto* tree = new QTreeWidget(&dlg);
    tree->setObjectName(QStringLiteral("quickLaunchAppTree"));
    tree->setColumnCount(2);
    tree->setHeaderLabels({QStringLiteral("名称 / 分组"), QStringLiteral("目标路径")});
    tree->header()->setStretchLastSection(true);
    tree->setColumnWidth(0, 220);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                          | QAbstractItemView::EditKeyPressed);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setDropIndicatorShown(true);
    tree->setAnimated(true);
    tree->setRootIsDecorated(true);
    tree->setItemDelegateForColumn(1, new QuickLaunchPathReadOnlyDelegate(tree));

    auto setupAppItemCheckable = [](QTreeWidgetItem* aItem) {
        aItem->setFlags((aItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable) & ~Qt::ItemIsAutoTristate);
        aItem->setCheckState(0, Qt::Unchecked);
    };

    QStringList groupOrder;
    QMap<QString, QVector<QuickLaunchApp>> grouped;
    for (const auto& app : m_quickLaunchApps) {
        const QString g = app.group.isEmpty() ? QStringLiteral("默认") : app.group;
        if (!grouped.contains(g))
            groupOrder.append(g);
        grouped[g].append(app);
    }
    for (const QString& gname : groupOrder) {
        auto* gItem = new QTreeWidgetItem(tree);
        gItem->setText(0, gname);
        gItem->setText(1, QString());
        gItem->setData(0, kQlKindRole, kQlKindGroup);
        for (const auto& app : grouped[gname]) {
            const QString disp = app.name.isEmpty() ? QFileInfo(app.path).completeBaseName() : app.name;
            auto* aItem = new QTreeWidgetItem(gItem);
            aItem->setText(0, disp);
            aItem->setText(1, app.path);
            aItem->setData(0, kQlKindRole, kQlKindApp);
            aItem->setData(0, kQlPathRole, app.path);
            setupAppItemCheckable(aItem);
        }
    }
    tree->expandAll();
    polishQuickLaunchTreeWidget(tree);
    new QuickLaunchVScrollRevealHelper(tree);
    mainLayout->addWidget(tree, 1);

    auto ensureDefaultGroup = [tree]() -> QTreeWidgetItem* {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* it = tree->topLevelItem(i);
            if (it->data(0, kQlKindRole).toString() == kQlKindGroup
                && it->text(0) == QStringLiteral("默认"))
                return it;
        }
        auto* g = new QTreeWidgetItem(tree);
        g->setText(0, QStringLiteral("默认"));
        g->setText(1, QString());
        g->setData(0, kQlKindRole, kQlKindGroup);
        return g;
    };

    auto resolveTargetGroup = [tree, ensureDefaultGroup]() -> QTreeWidgetItem* {
        auto* cur = tree->currentItem();
        if (cur) {
            if (cur->data(0, kQlKindRole).toString() == kQlKindGroup)
                return cur;
            if (cur->parent())
                return cur->parent();
        }
        if (tree->topLevelItemCount() > 0) {
            auto* first = tree->topLevelItem(0);
            if (first->data(0, kQlKindRole).toString() == kQlKindGroup)
                return first;
        }
        return ensureDefaultGroup();
    };

    auto collectTreeToApps = [tree](QVector<QuickLaunchApp>& out) {
        out.clear();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* top = tree->topLevelItem(i);
            if (top->data(0, kQlKindRole).toString() == kQlKindApp) {
                QuickLaunchApp app;
                app.name = top->text(0);
                app.path = top->data(0, kQlPathRole).toString();
                if (app.path.isEmpty())
                    app.path = top->text(1);
                app.group = QString();
                if (!app.path.isEmpty())
                    out.append(app);
                continue;
            }
            const QString groupName = top->text(0);
            const QString groupStored = (groupName == QStringLiteral("默认")) ? QString() : groupName;
            for (int j = 0; j < top->childCount(); ++j) {
                auto* ch = top->child(j);
                if (ch->data(0, kQlKindRole).toString() != kQlKindApp)
                    continue;
                QuickLaunchApp app;
                app.name = ch->text(0);
                app.path = ch->data(0, kQlPathRole).toString();
                if (app.path.isEmpty())
                    app.path = ch->text(1);
                app.group = groupStored;
                if (!app.path.isEmpty())
                    out.append(app);
            }
        }
    };

    auto* onlyRow = new QWidget(&dlg);
    auto* onlyLayout = new QHBoxLayout(onlyRow);
    onlyLayout->setContentsMargins(0, 0, 0, 0);
    onlyLayout->setSpacing(6);
    auto* onlyBox = new QCheckBox(QStringLiteral("只启动未运行的应用"), onlyRow);
    onlyBox->setObjectName(QStringLiteral("quickLaunchOnlyBox"));
    onlyBox->setAttribute(Qt::WA_StyledBackground, true);
    onlyBox->setChecked(m_quickLaunchOnlyIfNotRunning);
    onlyBox->setToolTip(
        QStringLiteral(
            "【如何判定】Windows 下两种方式（满足其一即跳过本次启动）：\n"
            "① tasklist 按 exe 名查进程；② 与「添加新窗口」相同的顶层窗口枚举——若已有带标题的可见主窗口且进程名匹配（含微信别名）。\n"
            ".lnk 会解析目标取文件名。\n\n"
            "【局限】仅托盘无可见主窗口、或窗口无标题被枚举过滤时，可能仍判定为未运行；"
            "若仍执行了启动，部分软件会主动新开窗口，无法单靠本项禁止。"));
    onlyLayout->addWidget(onlyBox);
    onlyLayout->addStretch(1);
    mainLayout->addWidget(onlyRow);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto* btnAdd = new QPushButton(QStringLiteral("添加应用..."), &dlg);
    btnAdd->setObjectName(QStringLiteral("quickLaunchAddButton"));
    auto* btnAddGroup = new QPushButton(QStringLiteral("添加分组..."), &dlg);
    btnAddGroup->setObjectName(QStringLiteral("quickLaunchAddGroupButton"));
    auto* btnChangeTarget = new QPushButton(QStringLiteral("更改目标..."), &dlg);
    btnChangeTarget->setObjectName(QStringLiteral("quickLaunchChangeTargetButton"));
    auto* btnScanRules = new QPushButton(QStringLiteral("黑白名单…"), &dlg);
    btnScanRules->setObjectName(QStringLiteral("quickLaunchScanRulesButton"));
    auto* btnDeleteChecked = new QPushButton(QStringLiteral("删除勾选"), &dlg);
    btnDeleteChecked->setObjectName(QStringLiteral("quickLaunchDeleteCheckedButton"));
    auto* btnDeleteAll = new QPushButton(QStringLiteral("删除全部"), &dlg);
    btnDeleteAll->setObjectName(QStringLiteral("quickLaunchDeleteAllButton"));
    auto* btnAutoScan = new QPushButton(QStringLiteral("自动扫描"), &dlg);
    btnAutoScan->setObjectName(QStringLiteral("quickLaunchAutoScanButton"));
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnAddGroup);
    btnRow->addWidget(btnChangeTarget);
    btnRow->addWidget(btnScanRules);
    btnRow->addStretch(1);
    btnRow->addWidget(btnDeleteChecked);
    btnRow->addWidget(btnDeleteAll);
    btnRow->addWidget(btnAutoScan);
    mainLayout->addLayout(btnRow);

    auto* btnRow2 = new QHBoxLayout();
    btnRow2->addStretch(1);
    auto* btnOk = new QPushButton(QStringLiteral("确定"), &dlg);
    btnOk->setObjectName(QStringLiteral("quickLaunchOkButton"));
    auto* btnCancel = new QPushButton(QStringLiteral("取消"), &dlg);
    btnCancel->setObjectName(QStringLiteral("quickLaunchCancelButton"));
    btnRow2->addWidget(btnOk);
    btnRow2->addWidget(btnCancel);
    mainLayout->addLayout(btnRow2);

    connect(btnAdd, &QPushButton::clicked, &dlg, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dlg,
            QStringLiteral("选择应用程序或快捷方式"),
            QString(),
            QStringLiteral("程序与快捷方式 (*.exe *.lnk);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        QFileInfo info(path);
        QTreeWidgetItem* g = resolveTargetGroup();
        auto* aItem = new QTreeWidgetItem(g);
        aItem->setText(0, info.completeBaseName());
        aItem->setText(1, path);
        aItem->setData(0, kQlKindRole, kQlKindApp);
        aItem->setData(0, kQlPathRole, path);
        setupAppItemCheckable(aItem);
        tree->setCurrentItem(aItem);
    });

    connect(btnScanRules, &QPushButton::clicked, &dlg, [&dlg, theme]() {
        QuickLaunchScanRulesDialog rulesDlg(&dlg, theme);
        rulesDlg.exec();
    });

    connect(btnAddGroup, &QPushButton::clicked, &dlg, [&]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            &dlg,
            QStringLiteral("新建分组"),
            QStringLiteral("分组名称："),
            QLineEdit::Normal,
            QString(),
            &ok);
        if (!ok)
            return;
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty())
            return;
        auto* g = new QTreeWidgetItem(tree);
        g->setText(0, trimmed);
        g->setText(1, QString());
        g->setData(0, kQlKindRole, kQlKindGroup);
        tree->setCurrentItem(g);
    });

    connect(btnChangeTarget, &QPushButton::clicked, &dlg, [&]() {
        auto* cur = tree->currentItem();
        if (!cur || cur->data(0, kQlKindRole).toString() != kQlKindApp) {
            QMessageBox::warning(&dlg, QStringLiteral("更改目标"),
                                 QStringLiteral("请先选中列表中的一条应用项（非分组行）。"));
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            &dlg,
            QStringLiteral("选择新的目标路径"),
            QFileInfo(cur->data(0, kQlPathRole).toString()).absolutePath(),
            QStringLiteral("程序与快捷方式 (*.exe *.lnk);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        QFileInfo info(path);
        cur->setData(0, kQlPathRole, path);
        cur->setText(1, path);
        if (cur->text(0).trimmed().isEmpty())
            cur->setText(0, info.completeBaseName());
    });

    connect(btnDeleteChecked, &QPushButton::clicked, &dlg, [&]() {
        QList<QTreeWidgetItem*> toDelete;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* top = tree->topLevelItem(i);
            if (!top)
                continue;
            if (top->data(0, kQlKindRole).toString() == kQlKindApp) {
                if (top->checkState(0) == Qt::Checked)
                    toDelete.append(top);
                continue;
            }
            for (int j = 0; j < top->childCount(); ++j) {
                auto* ch = top->child(j);
                if (ch && ch->data(0, kQlKindRole).toString() == kQlKindApp
                    && ch->checkState(0) == Qt::Checked)
                    toDelete.append(ch);
            }
        }
        if (toDelete.isEmpty()) {
            QMessageBox::information(&dlg, QStringLiteral("删除勾选"),
                                     QStringLiteral("请先勾选要删除的应用项。"));
            return;
        }
        for (auto* it : toDelete)
            delete it;
        QList<QTreeWidgetItem*> emptyGroups;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* g = tree->topLevelItem(i);
            if (g && g->data(0, kQlKindRole).toString() == kQlKindGroup && g->childCount() == 0)
                emptyGroups.append(g);
        }
        for (auto* g : emptyGroups)
            delete g;
    });

    connect(btnDeleteAll, &QPushButton::clicked, &dlg, [&]() {
        const auto r = QMessageBox::question(
            &dlg,
            QStringLiteral("删除全部"),
            QStringLiteral("确定清空列表中的所有分组与应用吗？此操作不可撤销（需点「确定」才会保存到配置）。"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (r != QMessageBox::Yes)
            return;
        tree->clear();
        ensureDefaultGroup();
    });

    connect(btnAutoScan, &QPushButton::clicked, &dlg, [&]() {
        // 1) Collect existing paths + resolved targets for dedup.
        QSet<QString> existingAbsPathsLower;
        QSet<QString> existingResolvedTargetsLower;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* top = tree->topLevelItem(i);
            if (!top) continue;
            auto scanItem = [&](QTreeWidgetItem* it) {
                if (!it) return;
                if (it->data(0, kQlKindRole).toString() != kQlKindApp)
                    return;
                const QString p = it->data(0, kQlPathRole).toString().isEmpty()
                                      ? it->text(1)
                                      : it->data(0, kQlPathRole).toString();
                const QString abs = QDir::cleanPath(QFileInfo(p).absoluteFilePath());
                if (!abs.isEmpty())
                    existingAbsPathsLower.insert(abs.toLower());
                const QString target = quickLaunchResolvedTargetForDedup(p);
                if (!target.isEmpty())
                    existingResolvedTargetsLower.insert(target.toLower());
            };
            if (top->data(0, kQlKindRole).toString() == kQlKindGroup) {
                for (int j = 0; j < top->childCount(); ++j)
                    scanItem(top->child(j));
            } else {
                scanItem(top);
            }
        }

        QSet<QString> scanForceInc;
        QSet<QString> scanForceExc;
        QuickLaunchScanMode scanMode = QuickLaunchScanMode::Strict;
        quickLaunchLoadScanRules(scanForceInc, scanForceExc, scanMode);

        const QStringList roots = quickLaunchScanRootDirs();

        QProgressDialog progress(QStringLiteral("正在自动扫描可添加的应用，可能需要一些时间..."),
                                 QStringLiteral("取消"),
                                 0, 0, &dlg);
        progress.setWindowTitle(QStringLiteral("自动扫描"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        progress.show();
        qApp->processEvents();

        auto findOrCreateGroupItem = [tree](const QString& groupName) -> QTreeWidgetItem* {
            const QString g = groupName.trimmed().isEmpty() ? QStringLiteral("默认") : groupName.trimmed();
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto* it = tree->topLevelItem(i);
                if (!it) continue;
                if (it->data(0, kQlKindRole).toString() == kQlKindGroup && it->text(0) == g)
                    return it;
            }
            auto* it = new QTreeWidgetItem(tree);
            it->setText(0, g);
            it->setText(1, QString());
            it->setData(0, kQlKindRole, kQlKindGroup);
            return it;
        };

        int addedCount = 0;
        int scannedCount = 0;
        int skippedByFilter = 0;

        for (const QString& root : roots) {
            if (progress.wasCanceled())
                break;
            const QDir rootDir(root);
            if (!rootDir.exists())
                continue;

            QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (progress.wasCanceled())
                    break;
                const QString lnkPath = it.next();
                ++scannedCount;
                if ((scannedCount % 50) == 0)
                    qApp->processEvents();

                const QString absLnk = QDir::cleanPath(QFileInfo(lnkPath).absoluteFilePath());
                if (absLnk.isEmpty())
                    continue;
                if (existingAbsPathsLower.contains(absLnk.toLower()))
                    continue;

                const QString target = quickLaunchResolvedTargetForDedup(absLnk);
                if (!target.isEmpty() && existingResolvedTargetsLower.contains(target.toLower()))
                    continue;

                if (!quickLaunchScanShouldIncludeWithOverrides(root, absLnk, target, scanMode, scanForceInc,
                                                             scanForceExc)) {
                    ++skippedByFilter;
                    continue;
                }

                const QString groupName = quickLaunchGroupFromShortcutLocation(root, absLnk);
                QTreeWidgetItem* gItem = findOrCreateGroupItem(groupName);
                const QString displayName = QFileInfo(absLnk).completeBaseName();
                auto* aItem = new QTreeWidgetItem(gItem);
                aItem->setText(0, displayName);
                aItem->setText(1, absLnk);
                aItem->setData(0, kQlKindRole, kQlKindApp);
                aItem->setData(0, kQlPathRole, absLnk);
                setupAppItemCheckable(aItem);

                existingAbsPathsLower.insert(absLnk.toLower());
                if (!target.isEmpty())
                    existingResolvedTargetsLower.insert(target.toLower());
                ++addedCount;
            }
        }

        progress.hide();

        QMessageBox info(&dlg);
        info.setWindowTitle(QStringLiteral("自动扫描完成"));
        info.setIcon(QMessageBox::Information);
        info.setText(
            QStringLiteral("扫描完成：新增 %1 项；共遍历 %2 个快捷方式（其中 %3 个未加入：含规则/黑白名单过滤或已存在去重）。")
                .arg(addedCount)
                .arg(scannedCount)
                .arg(skippedByFilter));
        info.setStandardButtons(QMessageBox::Ok);
        info.exec();
    });

    connect(btnOk, &QPushButton::clicked, &dlg, [&]() {
        collectTreeToApps(m_quickLaunchApps);
        m_quickLaunchOnlyIfNotRunning = onlyBox->isChecked();
        saveQuickLaunchConfig(m_quickLaunchApps, m_quickLaunchOnlyIfNotRunning);
        dlg.accept();
    });

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.setStyleSheet(ApplyStyle::quickLaunchManagerStyle(theme));
    dlg.exec();
}

static bool isProcessRunningByName(const QString& exeName)
{
    if (exeName.isEmpty())
        return false;
    QProcess proc;
    proc.start(QStringLiteral("cmd"),
               {QStringLiteral("/c"),
                QStringLiteral("tasklist /FI \"IMAGENAME eq %1\" /NH").arg(exeName)});
    if (!proc.waitForFinished(2000))
        return false;
    const QByteArray out = proc.readAllStandardOutput().toLower();
    return out.contains(exeName.toLower().toLatin1());
}

/// 与「只启动未运行」配合：同一款软件在任务管理器里可能对应多个 exe 名（如微信）
static QStringList quickLaunchExeAliasesForRunningCheck(const QString& exeFileName)
{
    QStringList all;
    if (exeFileName.isEmpty())
        return all;
    all.append(exeFileName);
    const QString low = exeFileName.toLower();
#ifdef Q_OS_WIN
    if (low.contains(QLatin1String("wechat")) || low.contains(QLatin1String("weixin"))) {
        const QStringList wx = { QStringLiteral("WeChat.exe"), QStringLiteral("Weixin.exe"),
                                 QStringLiteral("WeChatApp.exe") };
        for (const QString& w : wx) {
            bool dup = false;
            for (const QString& n : all) {
                if (n.compare(w, Qt::CaseInsensitive) == 0)
                    dup = true;
            }
            if (!dup)
                all.append(w);
        }
    }
#endif
    return all;
}

static bool quickLaunchIsAnyMatchingProcessRunning(const QString& exeFileName)
{
    for (const QString& n : quickLaunchExeAliasesForRunningCheck(exeFileName)) {
        if (isProcessRunningByName(n))
            return true;
    }
    return false;
}

#ifdef Q_OS_WIN
/// 与「添加新窗口」相同的 EnumWindows 规则：可见、无 Owner、非空标题等的顶层窗口，取其进程 exe 名
static QSet<QString> quickLaunchVisibleMainProcessNamesLower()
{
    QSet<QString> s;
    for (const WindowInfo& w : Win32WindowHelper::enumTopLevelWindows()) {
        if (!w.processName.isEmpty())
            s.insert(w.processName.toLower());
    }
    return s;
}

static bool quickLaunchHasEnumeratedVisibleWindowForExe(const QSet<QString>& visibleProcLower,
                                                        const QString& exeFileName)
{
    if (exeFileName.isEmpty() || visibleProcLower.isEmpty())
        return false;
    for (const QString& n : quickLaunchExeAliasesForRunningCheck(exeFileName)) {
        if (visibleProcLower.contains(n.toLower()))
            return true;
    }
    return false;
}
#endif

void MainWindow::runQuickLaunchApps()
{
    loadQuickLaunchConfig(m_quickLaunchApps, m_quickLaunchOnlyIfNotRunning);
    if (m_quickLaunchApps.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("尚未配置要快速启动的应用，右键「快速启动应用」可进行管理。"), 5000);
        return;
    }

    QSettings settings;
    const int maxLaunch = qBound(1, settings.value(QStringLiteral("quickLaunch/maxLaunchCount"), 10).toInt(), 30);
    const int totalItems = m_quickLaunchApps.size();
    const int sliceEnd = qMin(maxLaunch, totalItems);
    const bool hitCap = totalItems > maxLaunch;

    QStringList started;
    QStringList skipped;
    QStringList failed;

#ifdef Q_OS_WIN
    const QSet<QString> visibleProcLower = m_quickLaunchOnlyIfNotRunning
                                              ? quickLaunchVisibleMainProcessNamesLower()
                                              : QSet<QString>{};
#else
    const QSet<QString> visibleProcLower;
#endif

    for (int i = 0; i < sliceEnd; ++i) {
        const auto& app = m_quickLaunchApps[i];
        if (app.path.isEmpty())
            continue;
        const QString label = app.name.isEmpty() ? QFileInfo(app.path).fileName() : app.name;
        const QString exeName = quickLaunchExeNameForRunningCheck(app.path);
        bool alreadyRunning = false;
        if (m_quickLaunchOnlyIfNotRunning && !exeName.isEmpty()) {
            alreadyRunning = quickLaunchIsAnyMatchingProcessRunning(exeName);
#ifdef Q_OS_WIN
            if (!alreadyRunning)
                alreadyRunning = quickLaunchHasEnumeratedVisibleWindowForExe(visibleProcLower, exeName);
#endif
        }
        if (alreadyRunning) {
            skipped.append(QStringLiteral("%1 — %2").arg(label, app.path));
            continue;
        }
        QString err;
        if (!launchQuickLaunchPath(app.path, &err))
            failed.append(QStringLiteral("%1 — %2").arg(label, err.isEmpty() ? QStringLiteral("启动失败") : err));
        else
            started.append(QStringLiteral("%1 — %2").arg(label, app.path));
    }

    QStringList capSkippedLines;
    if (hitCap) {
        for (int i = maxLaunch; i < totalItems; ++i) {
            const auto& app = m_quickLaunchApps[i];
            const QString label = app.name.isEmpty() ? QFileInfo(app.path).fileName() : app.name;
            const QString pathPart = app.path.isEmpty() ? QStringLiteral("(无路径)") : app.path;
            capSkippedLines.append(QStringLiteral("%1 — %2").arg(label, pathPart));
        }
    }

    QString summary;
    if (!started.isEmpty())
        summary += QStringLiteral("已启动 %1 项").arg(started.size());
    if (!skipped.isEmpty()) {
        if (!summary.isEmpty())
            summary += QStringLiteral("；");
        summary += QStringLiteral("跳过 %1 项（已在运行）").arg(skipped.size());
    }
    if (!failed.isEmpty()) {
        if (!summary.isEmpty())
            summary += QStringLiteral("；");
        summary += QStringLiteral("失败 %1 项").arg(failed.size());
    }
    if (hitCap) {
        if (!summary.isEmpty())
            summary += QStringLiteral("；");
        summary += QStringLiteral("另有 %1 项因数量上限未尝试（当前上限 %2）")
                       .arg(totalItems - maxLaunch)
                       .arg(maxLaunch);
    }
    if (summary.isEmpty())
        summary = QStringLiteral("没有可启动的项。");

    statusBar()->showMessage(summary, 8000);

    QStringList detailLines;
    if (!started.isEmpty()) {
        detailLines << QStringLiteral("【已启动】");
        detailLines << started;
    }
    if (!skipped.isEmpty()) {
        if (!detailLines.isEmpty())
            detailLines << QString();
        detailLines << QStringLiteral("【已跳过】");
        detailLines << skipped;
    }
    if (!failed.isEmpty()) {
        if (!detailLines.isEmpty())
            detailLines << QString();
        detailLines << QStringLiteral("【失败】");
        detailLines << failed;
    }
    if (!capSkippedLines.isEmpty()) {
        if (!detailLines.isEmpty())
            detailLines << QString();
        detailLines << QStringLiteral("【因数量上限未尝试】（仅处理列表前 %1 项）").arg(maxLaunch);
        detailLines << capSkippedLines;
    }

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("快速启动结果"));
    box.setIcon(QMessageBox::Information);
    box.setText(summary);
    if (!detailLines.isEmpty())
        box.setDetailedText(detailLines.join(QStringLiteral("\n")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::openAppHelpDialog()
{
    HelpCenterDialog::openUsageGuide(this);
}

void MainWindow::openBugLogDialog()
{
    HelpCenterDialog::openBugLog(this);
}

void MainWindow::startBatchAddWindows(const QVector<WindowInfo>& list)
{
    if (list.isEmpty()) return;

    m_batchAddIndex = 0;
    m_batchAddList = list;
    m_batchAddIndex = 0;
    m_batchAddSuccessCount = 0;

    if (!m_batchAddOverlay) {
        m_batchAddOverlay = new BatchAddOverlayWidget(nullptr);
        auto* layout = new QVBoxLayout(m_batchAddOverlay);
        layout->setAlignment(Qt::AlignCenter);
        layout->setContentsMargins(24, 24, 24, 24);
        m_batchAddPrompt = new QLabel(m_batchAddOverlay);
        m_batchAddPrompt->setObjectName("batchAddPrompt");
        m_batchAddPrompt->setStyleSheet(
            "QLabel#batchAddPrompt { background-color: rgba(255, 255, 255, 0.95); "
            "color: #333; font-size: 16px; padding: 20px 32px; border-radius: 12px; }");
        m_batchAddPrompt->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_batchAddPrompt, 0, Qt::AlignHCenter);
    }

    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    m_batchAddOverlay->setGeometry(screenRect);
    m_batchAddPrompt->setText(QStringLiteral("正在添加 0/%1...").arg(list.size()));
    m_batchAddOverlay->show();
    m_batchAddOverlay->raise();
    m_batchAddOverlay->activateWindow();

    QTimer::singleShot(80, this, &MainWindow::processNextBatchAdd);
}

void MainWindow::processNextBatchAdd()
{
    if (m_batchAddIndex >= m_batchAddList.size()) {
        if (!m_batchAddOverlay || !m_batchAddPrompt) return;
        m_batchAddPrompt->setText(QStringLiteral("共 %1 个，成功添加 %2 个")
                                      .arg(m_batchAddList.size()).arg(m_batchAddSuccessCount));
        const int total = m_batchAddList.size();
        const int success = m_batchAddSuccessCount;
        m_batchAddList.clear();
        m_batchAddIndex = -1;
        m_batchAddSuccessCount = 0;
        QTimer::singleShot(2000, this, [this, total, success]() {
            if (m_batchAddIndex < 0 && m_batchAddOverlay) {
                m_batchAddOverlay->deleteLater();
                m_batchAddOverlay = nullptr;
                m_batchAddPrompt = nullptr;
            }
            if (success == 0)
                showSystemReadyPage();
        });
        return;
    }

    const WindowInfo& info = m_batchAddList.at(m_batchAddIndex);
    bool ok = false;
    if (info.handle != 0 && Win32WindowHelper::isWindowValid(info.handle)) {
        addWindowToPlatform(info);
        ok = true;
        ++m_batchAddSuccessCount;
    }
    ++m_batchAddIndex;

    if (m_batchAddPrompt) {
        m_batchAddPrompt->setText(QStringLiteral("正在添加 %1/%2...")
                                      .arg(m_batchAddIndex).arg(m_batchAddList.size()));
    }
    QTimer::singleShot(ok ? 120 : 50, this, &MainWindow::processNextBatchAdd);
}

// ==================== Window Management ====================

WindowDisplayMode MainWindow::determineDisplayMode(const WindowInfo& info)
{
    QString proc = info.processName.toLower();
    if (proc.contains("aliworkbench")
        || proc.contains("aliim") || proc.contains("qianniu"))
        return WindowDisplayMode::Embed;
    return WindowDisplayMode::FloatFollow;
}

bool MainWindow::isWechatWindowInfo(const WindowInfo& info)
{
    const QString proc = info.processName.toLower();
    const QString title = info.platformName.toLower();
    return proc.contains(QStringLiteral("wechat"))
           || title.contains(QStringLiteral("wechat"))
           || info.platformName.contains(QStringLiteral("微信"));
}

QSet<quintptr> MainWindow::managedWindowHandles() const
{
    QSet<quintptr> set;
    for (auto it = m_managedWindows.constBegin(); it != m_managedWindows.constEnd(); ++it)
        set.insert(it.value().handle);
    return set;
}

QString MainWindow::matchCustomerServicePlatform(const WindowInfo& info) const
{
    if (info.platformName.contains(QStringLiteral("千牛"))) return QStringLiteral("qianniu");
    if (info.platformName.contains(QStringLiteral("拼多多"))) return QStringLiteral("pinduoduo");
    if (info.platformName.contains(QStringLiteral("抖店"))) return QStringLiteral("douyin");

    QString proc = info.processName.toLower();
    if (proc.contains("aliworkbench") || proc.contains("aliim") || proc.contains("qianniu"))
        return QStringLiteral("qianniu");
    if (proc.contains("pinduoduo") || proc.contains("pdd"))
        return QStringLiteral("pinduoduo");
    if (proc.contains("douyin") || proc.contains("feige") || proc.contains("jinritemai"))
        return QStringLiteral("douyin");

    return {};
}

QStandardItem* MainWindow::findGroupItem(const QString& groupId) const
{
    for (int i = 0; i < m_platformTreeModel->rowCount(); ++i) {
        auto* item = m_platformTreeModel->item(i);
        if (item && item->data(PlatformIdRole).toString() == groupId)
            return item;
    }
    return nullptr;
}

QStandardItem* MainWindow::findChildItem(QStandardItem* parent, const QString& platformId) const
{
    if (!parent) return nullptr;
    for (int i = 0; i < parent->rowCount(); ++i) {
        auto* child = parent->child(i);
        if (child && child->data(PlatformIdRole).toString() == platformId)
            return child;
    }
    return nullptr;
}

void MainWindow::addWindowToPlatform(const WindowInfo& info)
{
    WindowDisplayMode mode = determineDisplayMode(info);
    QString csMatch = matchCustomerServicePlatform(info);
    bool isCS = !csMatch.isEmpty();
    QString platformId;

    if (isCS) {
        platformId = csMatch;
        // If already has a window, detach old one first
        if (m_managedWindows.contains(platformId)) {
            auto& old = m_managedWindows[platformId];
            if (old.wasSetup) {
                if (old.mode == WindowDisplayMode::Embed)
                    Win32WindowHelper::detachWindow(old.handle);
                else
                    Win32WindowHelper::detachFloatFollow(old.handle,
                                                         true,
                                                         old.useFloatToolWindow,
                                                         old.useFloatOwner);
            }
            if (old.stackPage) {
                m_centerStack->removeWidget(old.stackPage);
                old.stackPage->deleteLater();
            }
            m_managedWindows.remove(platformId);
        }
        // Update tree item to activated
        auto* csItem = findChildItem(m_csGroup, platformId);
        if (csItem) {
            csItem->setData(true, IsActivatedRole);
        }
        // Update group dot to green if at least one is activated
        m_csGroup->setData(QColor(82, 196, 26), DotColorRole);
        qInfo() << "[MainWindow] 客服平台关联:" << platformId << "<-" << info.platformName;
    } else {
        platformId = QStringLiteral("online_%1").arg(m_nextOnlineId++);

        // Add to tree under online group
        QIcon icon = Win32WindowHelper::windowIcon(info);
        if (icon.isNull())
            icon = onlinePlatformFallbackIcon(info);
        if (icon.isNull())
            icon = qApp->style()->standardIcon(QStyle::SP_ComputerIcon);
        auto* item = new QStandardItem(icon, info.platformName);
        item->setData(platformId, PlatformIdRole);
        item->setData(false, IsGroupRole);
        item->setData(false, IsCustomerServiceItemRole);
        item->setData(true, IsActivatedRole);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        m_onlineGroup->appendRow(item);
        m_platformTree->expand(m_onlineGroup->index());
        qInfo() << "[MainWindow] 在线平台添加:" << platformId << "=" << info.platformName;
    }

    // Create stack page
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("page_%1").arg(platformId));
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    EmbeddedWindowContainer* container = nullptr;
    if (mode == WindowDisplayMode::Embed) {
        container = new EmbeddedWindowContainer(page);
        container->setObjectName("embedContainer");
        pageLayout->addWidget(container);
    } else {
        auto* floatLabel = new QLabel(page);
        floatLabel->setAlignment(Qt::AlignCenter);
        floatLabel->setText(QStringLiteral("浮窗模式 — %1").arg(info.platformName));
        floatLabel->setStyleSheet("color: #999; font-size: 14px;");
        pageLayout->addWidget(floatLabel);
    }
    m_centerStack->addWidget(page);

    // Store entry
    ManagedWindowEntry entry;
    entry.platformName = info.platformName;
    entry.platformId = platformId;
    entry.handle = info.handle;
    entry.mode = mode;
    entry.isCustomerService = isCS;
    if (mode == WindowDisplayMode::FloatFollow && isWechatWindow(info)) {
        entry.useFloatOwner = false;
        entry.useFloatToolWindow = false;
        entry.useFloatRaiseAbove = true;
    }
    entry.container = container;
    entry.stackPage = page;
    entry.wasSetup = false;
    m_managedWindows[platformId] = entry;

    updateTreeViewHeight();

    // Auto-select the newly added item
    QStandardItem* targetItem = nullptr;
    if (isCS) {
        targetItem = findChildItem(m_csGroup, platformId);
    } else {
        targetItem = findChildItem(m_onlineGroup, platformId);
    }
    if (targetItem) {
        m_platformTree->setCurrentIndex(targetItem->index());
    }
}

void MainWindow::switchToWindow(const QString& platformId)
{
    if (!m_managedWindows.contains(platformId)) return;
    auto& entry = m_managedWindows[platformId];

    if (!Win32WindowHelper::isWindowValid(entry.handle)) {
        qWarning() << "[MainWindow] 窗口已失效:" << entry.platformName;
        removeOnlinePlatformItem(platformId);
        return;
    }

    {
        const QRect windowRect = Win32WindowHelper::windowRect(entry.handle);
        const unsigned int dpi = Win32WindowHelper::windowDpi(entry.handle);
        qInfo() << "[MainWindow] 切换外部窗口:"
                << "platformId=" << platformId
                << "name=" << entry.platformName
                << "mode=" << (entry.mode == WindowDisplayMode::Embed ? "Embed" : "FloatFollow")
                << "handle=0x" << QString::number(static_cast<qulonglong>(entry.handle), 16)
                << "dpi=" << dpi
                << "windowRect(" << formatRect(windowRect) << ")";
    }

    // Hide current float if switching away
    hideCurrentFloatWindow();

    // First-time setup
    if (!entry.wasSetup) {
        if (entry.mode == WindowDisplayMode::Embed && entry.container) {
            auto* ec = static_cast<EmbeddedWindowContainer*>(entry.container);
            ec->setEmbeddedHandle(entry.handle);
            qInfo() << "[MainWindow] 嵌入窗口:" << entry.platformName;
        } else if (entry.mode == WindowDisplayMode::FloatFollow) {
            Win32WindowHelper::setupFloatFollow(entry.handle,
                                                (quintptr)winId(),
                                                entry.useFloatOwner,
                                                entry.useFloatToolWindow);
            qInfo() << "[MainWindow] 浮窗跟随设置:"
                    << "name=" << entry.platformName
                    << "useOwner=" << entry.useFloatOwner
                    << "useToolWindow=" << entry.useFloatToolWindow
                    << "raiseAbove=" << entry.useFloatRaiseAbove;
        }
        entry.wasSetup = true;
    }

    // Switch stack
    m_centerStack->setCurrentWidget(entry.stackPage);
    m_activeWindowId = platformId;

    // Ensure embedded window填满 CenterContent（解决首次嵌入宽高为 0 的问题）
    if (entry.mode == WindowDisplayMode::Embed && entry.container && entry.wasSetup) {
        QTimer::singleShot(0, this, [this, platformId]() {
            if (!m_managedWindows.contains(platformId)) return;
            auto& currentEntry = m_managedWindows[platformId];
            if (currentEntry.handle && currentEntry.container) {
                Win32WindowHelper::resizeEmbeddedWindow(currentEntry.handle, currentEntry.container);
                QPoint topLeft = currentEntry.container->mapToGlobal(QPoint(0, 0));
                currentEntry.lastDisplayGeometry = QRect(topLeft, currentEntry.container->size());
            }
        });
    }

    // Show float window if needed
    if (entry.mode == WindowDisplayMode::FloatFollow) {
        updateFloatFollowPosition();
    }
}

void MainWindow::hideCurrentFloatWindow()
{
    if (m_activeWindowId.isEmpty()) return;
    if (!m_managedWindows.contains(m_activeWindowId)) return;

    auto& entry = m_managedWindows[m_activeWindowId];
    if (entry.mode == WindowDisplayMode::FloatFollow && entry.wasSetup) {
        // For WeChat: don't SW_HIDE, keep it renderable for PrintWindow-based OCR.
        // Move it far off-screen instead (still non-activated).
        if (entry.platformName.contains(QStringLiteral("微信"))) {
            const int w = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.width() : 800;
            const int h = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.height() : 600;
            Win32WindowHelper::showWindowAt(entry.handle, -20000, -20000, w, h, false);
        } else {
            Win32WindowHelper::hideWindow(entry.handle);
        }
    }
}

void MainWindow::checkManagedWindowsState()
{
    if (m_managedWindows.isEmpty()) {
        return;
    }

    QStringList invalidPlatformIds;
    for (auto it = m_managedWindows.cbegin(); it != m_managedWindows.cend(); ++it) {
        if (!Win32WindowHelper::isWindowValid(it.value().handle)) {
            invalidPlatformIds.append(it.key());
        }
    }

    if (invalidPlatformIds.isEmpty() && !m_activeWindowId.isEmpty()
        && m_managedWindows.contains(m_activeWindowId)) {
        auto& activeEntry = m_managedWindows[m_activeWindowId];
        const bool isInvisible = !Win32WindowHelper::isWindowVisible(activeEntry.handle);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool usesCloseIntentDetection = activeEntry.mode == WindowDisplayMode::FloatFollow;

        if (usesCloseIntentDetection && activeEntry.wasSetup) {
            const QRect currentRect = displayRectForEntry(activeEntry);
            const QPoint cursorPos = QCursor::pos();
            const bool inCloseHotspot = isInWindowCloseHotspot(currentRect, cursorPos);
            const bool overManagedWindow = Win32WindowHelper::isPointInsideWindow(activeEntry.handle, cursorPos);
            const bool leftButtonPressed = Win32WindowHelper::isLeftMouseButtonPressed();
            if (!isInvisible && inCloseHotspot && overManagedWindow && leftButtonPressed) {
                if (activeEntry.closeIntentSinceMs == 0) {
                    activeEntry.closeIntentSinceMs = nowMs;
                    if (m_centerStack->currentWidget() == activeEntry.stackPage) {
                        showSystemReadyPage();
                    }
                }
            } else if (activeEntry.closeIntentSinceMs > 0
                       && (nowMs - activeEntry.closeIntentSinceMs) > 1500) {
                activeEntry.closeIntentSinceMs = 0;
            }
        }

        if (!isInvisible) {
            activeEntry.invisibleSinceMs = 0;
        } else if (activeEntry.invisibleSinceMs == 0) {
            activeEntry.invisibleSinceMs = nowMs;
        }

        const bool invisibleForLongEnough = activeEntry.invisibleSinceMs > 0
                                            && (nowMs - activeEntry.invisibleSinceMs) >= 600;
        const bool invisibleAfterCloseIntent = usesCloseIntentDetection
                                               && activeEntry.closeIntentSinceMs > 0
                                               && activeEntry.invisibleSinceMs > 0
                                               && activeEntry.invisibleSinceMs >= activeEntry.closeIntentSinceMs
                                               && (nowMs - activeEntry.closeIntentSinceMs) <= 1500;

        const qint64 elapsedSinceCloseIntent = activeEntry.closeIntentSinceMs > 0
                                                   ? (nowMs - activeEntry.closeIntentSinceMs) : 0;
        const bool gracePeriodElapsed = usesCloseIntentDetection
                                        && activeEntry.closeIntentSinceMs > 0
                                        && elapsedSinceCloseIntent >= 800
                                        && isInvisible;

        const bool shouldDisconnect = usesCloseIntentDetection
                                          ? (invisibleAfterCloseIntent || gracePeriodElapsed)
                                          : invisibleForLongEnough;

        if (activeEntry.wasSetup && shouldDisconnect) {
            invalidPlatformIds.append(m_activeWindowId);
        }
    }

    if (invalidPlatformIds.isEmpty()) {
        return;
    }

    invalidPlatformIds.removeDuplicates();
    for (const QString& platformId : invalidPlatformIds) {
        if (!m_managedWindows.contains(platformId)) {
            continue;
        }

        const QString platformName = m_managedWindows[platformId].platformName;
        qInfo() << "[MainWindow] 检测到外部窗口已关闭或隐藏，自动断开:" << platformId << platformName;
        removeOnlinePlatformItem(platformId, false, false);
        statusBar()->showMessage(QStringLiteral("已检测到“%1”窗口关闭或隐藏，已自动断开关联")
                                     .arg(platformName),
                                 3000);
    }
}

void MainWindow::updateFloatFollowPosition()
{
    if (m_activeWindowId.isEmpty()) return;
    if (!m_managedWindows.contains(m_activeWindowId)) return;

    auto& entry = m_managedWindows[m_activeWindowId];
    if (entry.mode != WindowDisplayMode::FloatFollow) return;
    if (!entry.wasSetup) return;
    if (entry.closeIntentSinceMs > 0) return;

    if (isMinimized() || m_centerStack->currentWidget() != entry.stackPage) {
        // For WeChat: keep window renderable for background OCR (PrintWindow).
        if (entry.platformName.contains(QStringLiteral("微信"))) {
            const int w = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.width() : 800;
            const int h = entry.lastDisplayGeometry.isValid() ? entry.lastDisplayGeometry.height() : 600;
            Win32WindowHelper::showWindowAt(entry.handle, -20000, -20000, w, h, false);
        } else {
            Win32WindowHelper::hideWindow(entry.handle);
        }
        return;
    }

    QPoint logicalTopLeft = m_centerStack->mapToGlobal(QPoint(0, 0));
    QSize logicalSize = m_centerStack->size();
    QScreen* screen = m_centerStack->screen();
    if (!screen && windowHandle()) {
        screen = windowHandle()->screen();
    }

    const qreal scale = screen ? screen->devicePixelRatio() : 1.0;
    const QRect targetWindowRect(qRound(logicalTopLeft.x() * scale),
                                 qRound(logicalTopLeft.y() * scale),
                                 qRound(logicalSize.width() * scale),
                                 qRound(logicalSize.height() * scale));

    entry.lastDisplayGeometry = targetWindowRect;
    entry.invisibleSinceMs = 0;
    entry.closeIntentSinceMs = 0;
    {
        const QRect windowRect = Win32WindowHelper::windowRect(entry.handle);
        const unsigned int dpi = Win32WindowHelper::windowDpi(entry.handle);
        qInfo() << "[MainWindow] 浮窗跟随定位:"
                << "platformId=" << m_activeWindowId
                << "name=" << entry.platformName
                << "handle=0x" << QString::number(static_cast<qulonglong>(entry.handle), 16)
                << "dpi=" << dpi
                << "screenScale=" << scale
                << "centerStackLogical(" << formatRect(QRect(logicalTopLeft, logicalSize)) << ")"
                << "targetRect(" << formatRect(targetWindowRect) << ")"
                << "windowRectBefore(" << formatRect(windowRect) << ")";
    }
    Win32WindowHelper::showWindowAt(entry.handle,
                                    entry.lastDisplayGeometry.x(),
                                    entry.lastDisplayGeometry.y(),
                                    entry.lastDisplayGeometry.width(),
                                    entry.lastDisplayGeometry.height(),
                                    entry.useFloatRaiseAbove);
}

QRect MainWindow::displayRectForEntry(const ManagedWindowEntry& entry) const
{
    if (entry.mode == WindowDisplayMode::Embed && entry.container && entry.container->isVisible()) {
        QPoint topLeft = entry.container->mapToGlobal(QPoint(0, 0));
        return QRect(topLeft, entry.container->size());
    }

    if (entry.mode == WindowDisplayMode::FloatFollow && entry.wasSetup
        && m_activeWindowId == entry.platformId) {
        QRect rect = Win32WindowHelper::windowRect(entry.handle);
        if (rect.isValid()) {
            return rect;
        }
    }

    return entry.lastDisplayGeometry;
}

void MainWindow::releaseManagedWindow(ManagedWindowEntry& entry,
                                      bool keepVisible,
                                      bool showWindowAfterRelease)
{
    if (!entry.wasSetup) return;

    const QRect targetRect = displayRectForEntry(entry);
    const bool isWechatSpecialFloat = entry.mode == WindowDisplayMode::FloatFollow
                                      && !entry.useFloatOwner
                                      && !entry.useFloatToolWindow;

    if (entry.mode == WindowDisplayMode::Embed) {
        Win32WindowHelper::detachWindow(entry.handle, targetRect);
    } else if (isWechatSpecialFloat) {
        if (showWindowAfterRelease && keepVisible && targetRect.isValid()) {
            Win32WindowHelper::showWindowAt(entry.handle,
                                            targetRect.x(),
                                            targetRect.y(),
                                            targetRect.width(),
                                            targetRect.height(),
                                            entry.useFloatRaiseAbove);
        } else if (!showWindowAfterRelease) {
            Win32WindowHelper::hideWindow(entry.handle);
        }
    } else {
        const bool restoreTaskbarEntry = entry.useFloatToolWindow
                                             ? showWindowAfterRelease
                                             : true;
        Win32WindowHelper::detachFloatFollow(entry.handle,
                                             showWindowAfterRelease,
                                             restoreTaskbarEntry,
                                             entry.useFloatOwner);
        if (keepVisible && showWindowAfterRelease && targetRect.isValid()) {
            Win32WindowHelper::showWindowAt(entry.handle, targetRect.x(), targetRect.y(),
                                            targetRect.width(), targetRect.height());
        }
    }

    if (!keepVisible && showWindowAfterRelease) {
        Win32WindowHelper::minimizeWindow(entry.handle);
    }
}

void MainWindow::removeOnlinePlatformItem(const QString& platformId,
                                          bool keepVisible,
                                          bool showWindowAfterRelease)
{
    if (!m_managedWindows.contains(platformId)) return;
    auto& entry = m_managedWindows[platformId];

    const bool wasActive = (m_activeWindowId == platformId);
    if (wasActive) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
    }

    releaseManagedWindow(entry, keepVisible, showWindowAfterRelease);

    // Remove stack page
    if (entry.stackPage) {
        m_centerStack->removeWidget(entry.stackPage);
        entry.stackPage->deleteLater();
    }

    // Remove tree item
    if (entry.isCustomerService) {
        auto* csItem = findChildItem(m_csGroup, platformId);
        if (csItem) csItem->setData(false, IsActivatedRole);
    } else {
        auto* item = findChildItem(m_onlineGroup, platformId);
        if (item) m_onlineGroup->removeRow(item->row());
    }

    m_managedWindows.remove(platformId);
    updateTreeViewHeight();
    // 删除「当前正在显示」的托管项时，removeRow 会先触发一次 currentChanged；
    // 若此处再无条件 showSystemReadyPage()，会覆盖已切到的下一项（如微信）的 stack，
    // 造成树选中与 m_centerStack、m_activeWindowId 不一致，随后 move 时
    // updateFloatFollowPosition 会把 FloatFollow 微信误判为「非当前页」而挪到屏外。
    // 在数据与树结构都更新完后，再按当前选中项统一同步一次中间区域（幂等）。
    if (wasActive)
        onPlatformTreeSelectionChanged();
    qInfo() << "[MainWindow] 移除平台:" << platformId;
}

void MainWindow::detachAllWindows()
{
    for (auto it = m_managedWindows.begin(); it != m_managedWindows.end(); ++it) {
        auto& entry = it.value();
        if (!entry.wasSetup) continue;
        const bool keepVisible = (entry.platformId == m_activeWindowId);
        releaseManagedWindow(entry, keepVisible);
    }
    m_managedWindows.clear();
    m_activeWindowId.clear();
}

// ==================== Context Menu ====================

void MainWindow::showPlatformContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_platformTree->indexAt(pos);
    if (!idx.isValid()) return;

    QString id = idx.data(PlatformIdRole).toString();
    bool isCS = idx.data(IsCustomerServiceItemRole).toBool();
    bool isGroup = idx.data(IsGroupRole).toBool();
    if (isGroup) {
        // Group node context menu (e.g. "在线平台")
        if (id == QLatin1String("online")) {
            QList<QString> onlinePlatformIds;
            for (auto it = m_managedWindows.cbegin(); it != m_managedWindows.cend(); ++it) {
                if (!it.value().isCustomerService) {
                    onlinePlatformIds.append(it.key());
                }
            }

            if (onlinePlatformIds.isEmpty())
                return;

            QMenu menu(this);
            QAction* actRemoveAll = menu.addAction(QStringLiteral("删除全部平台"));
            QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
            if (chosen == actRemoveAll) {
                const QMessageBox::StandardButton btn = QMessageBox::question(
                    this,
                    QStringLiteral("确认"),
                    QStringLiteral("确定要删除“在线平台”下的所有平台项吗？删除后仅解除关联，不会关闭外部窗口。"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);

                if (btn == QMessageBox::Yes) {
                    // Copy list first: removeOnlinePlatformItem will mutate tree/model.
                    const auto ids = onlinePlatformIds;
                    for (const QString& pid : ids) {
                        removeOnlinePlatformItem(pid, true, true);
                    }

                    m_activeWindowId.clear();
                    showSystemReadyPage();
                }
            }
        }
        // Customer service group: "客服平台"
        else if (id == QLatin1String("cs")) {
            QList<QString> csPlatformIds;
            for (auto it = m_managedWindows.cbegin(); it != m_managedWindows.cend(); ++it) {
                if (it.value().isCustomerService) {
                    csPlatformIds.append(it.key());
                }
            }

            if (csPlatformIds.isEmpty())
                return;

            QMenu menu(this);
            QAction* actDisconnectAll = menu.addAction(QStringLiteral("断开所有关联"));
            QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
            if (chosen == actDisconnectAll) {
                const QMessageBox::StandardButton btn = QMessageBox::question(
                    this,
                    QStringLiteral("确认"),
                    QStringLiteral("确定要断开“客服平台”下所有已关联平台的关联吗？断开后仅解除与本软件的嵌入/跟随关联，不会关闭外部窗口。"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);

                if (btn == QMessageBox::Yes) {
                    const auto ids = csPlatformIds;
                    for (const QString& pid : ids) {
                        removeOnlinePlatformItem(pid, true, true);
                    }
                    m_activeWindowId.clear();
                    showSystemReadyPage();
                }
            }
        }
        return;
    }

    if (!m_managedWindows.contains(id)) return;

    QMenu menu(this);
    QAction* actPrimary = nullptr;
    if (isCS)
        actPrimary = menu.addAction(QStringLiteral("断开关联"));
    else
        actPrimary = menu.addAction(QStringLiteral("删除"));

    QAction* actCalibrateWechat = nullptr;
    QAction* actCalibrateQianniu = nullptr;
    QAction* actCalibratePdd = nullptr;
    {
        const quintptr hwnd = m_managedWindows[id].handle;
        if (managedWindowShowsWechatOcrCalibration(hwnd)) {
            actCalibrateWechat = menu.addAction(QStringLiteral("微信OCR校准（备用方案）"));
        }
        if (managedWindowShowsQianniuOcrCalibration(hwnd)) {
            actCalibrateQianniu = menu.addAction(QStringLiteral("千牛OCR区域校准"));
        }
        if (managedWindowShowsPddOcrCalibration(hwnd)) {
            actCalibratePdd = menu.addAction(QStringLiteral("拼多多OCR区域校准"));
        }
    }

    QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
    if (chosen == actPrimary) {
        removeOnlinePlatformItem(id);
    } else if (actCalibrateWechat && chosen == actCalibrateWechat) {
        startWechatRpaCalibration(id);
    } else if (actCalibrateQianniu && chosen == actCalibrateQianniu) {
        startQianniuRpaCalibration(id);
    } else if (actCalibratePdd && chosen == actCalibratePdd) {
        startPddRpaCalibration(id);
    }
}

void MainWindow::startWechatRpaCalibration(const QString& platformId)
{
    if (!m_managedWindows.contains(platformId))
        return;

    const auto& entry = m_managedWindows[platformId];
    if (!Win32WindowHelper::isWindowValid(entry.handle)) {
        qWarning() << "[MainWindow] 微信RPA校准失败：窗口无效";
        return;
    }
    startWechatRpaCalibrationByHwnd(entry.handle);
}

void MainWindow::startWechatRpaCalibrationStandalone()
{
    const quintptr hwnd = findWechatCalibrationWindow();
    if (!Win32WindowHelper::isWindowValid(hwnd)) {
        QMessageBox::warning(this,
                             QStringLiteral("微信 OCR 校准"),
                             QStringLiteral("未找到可用的微信主窗口。\n请先打开微信并确保窗口可见、未最小化。"));
        return;
    }
    startWechatRpaCalibrationByHwnd(hwnd);
}

void MainWindow::startWechatRpaCalibrationByHwnd(quintptr hwnd)
{
    if (!Win32WindowHelper::isWindowValid(hwnd)) {
        QMessageBox::warning(this,
                             QStringLiteral("微信 OCR 校准"),
                             QStringLiteral("微信窗口无效，请重新打开微信后再试。"));
        return;
    }

    QStringList labels;
    const auto& specs = wechatCalibrationRegionSpecs();
    for (const auto& spec : specs)
        labels.append(spec.label);

    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this,
        QStringLiteral("微信 OCR 校准"),
        QStringLiteral("请选择要校准的区域："),
        labels,
        0,
        false,
        &ok);
    if (!ok || choice.isEmpty())
        return;

    const WechatCalibrationRegionSpec* spec = wechatCalibrationSpecByLabel(choice);
    if (!spec) {
        QMessageBox::warning(this,
                             QStringLiteral("微信 OCR 校准"),
                             QStringLiteral("未知的微信校准区域，请重试。"));
        return;
    }

    const QRect wr = Win32WindowHelper::windowRect(hwnd);
    if (!wr.isValid()) {
        QMessageBox::warning(this,
                             QStringLiteral("微信 OCR 校准"),
                             QStringLiteral("无法读取微信窗口位置，请重试。"));
        return;
    }
    Win32WindowHelper::showWindowAt(hwnd, wr.x(), wr.y(), wr.width(), wr.height(), true);

    QScreen* baseScreen = QGuiApplication::screenAt(wr.center());
    if (!baseScreen)
        baseScreen = QGuiApplication::primaryScreen();
    const QRect screenGeom = baseScreen ? baseScreen->geometry() : QRect();
    if (!screenGeom.isValid()) {
        QMessageBox::warning(this,
                             QStringLiteral("微信 OCR 校准"),
                             QStringLiteral("无法确定微信所在屏幕，请重试。"));
        return;
    }

    auto* overlay = new RpaRegionCalibrationOverlay(nullptr);
    overlay->setAttribute(Qt::WA_NativeWindow, true);
    overlay->setHelpTip(spec->helpTip);
    overlay->setDimColor(QColor(24, 26, 32, 72));
    overlay->setGeometry(screenGeom);
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();

    qInfo() << "[MainWindow] 微信 OCR 校准开始 handle=0x"
            << QString::number(static_cast<qulonglong>(hwnd), 16)
            << "region=" << spec->id
            << "screenGeom(" << formatRect(screenGeom) << ")";

    overlay->setOnFinished([this, hwnd, spec, overlay](bool ok2, QRect sel) {
        if (!ok2) {
            qInfo() << "[MainWindow] 微信 OCR 校准已取消 region=" << spec->id;
            return;
        }
        if (!Win32WindowHelper::isWindowValid(hwnd)) {
            qWarning() << "[MainWindow] 微信 OCR 校准失败：窗口已无效";
            return;
        }

        QScreen* sc = overlay->screen();
        const qreal dpr = sc ? sc->devicePixelRatio() : 1.0;
        const QRect mapped = Win32WindowHelper::mapOverlaySelectionToTargetWindowRelative(
            overlay->winId(), hwnd, sel, dpr, overlay->size());
        if (mapped.width() < spec->minWidth || mapped.height() < spec->minHeight) {
            qWarning() << "[MainWindow] 微信 OCR 校准映射失败 region=" << spec->id
                       << "sel=" << formatRect(sel)
                       << "mapped=" << formatRect(mapped);
            QMessageBox::warning(
                this,
                QStringLiteral("微信 OCR 校准"),
                QStringLiteral("选区映射失败或过小，请重试。"));
            return;
        }

        const bool saved = mergeWriteWechatRpaConfig(hwnd, spec->id, mapped);
        qInfo() << "[MainWindow] 微信 OCR 校准结果 region=" << spec->id
                << "mapped(" << formatRect(mapped) << ")"
                << "saved=" << saved;
        overlay->hide();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        statusBar()->showMessage(
            saved
                ? QStringLiteral("已写入 python/rpa/config/wechat_config.json：%1").arg(spec->label)
                : QStringLiteral("微信 OCR 配置写入失败，请查看日志"),
            5000);
        if (!saved)
            return;
        if (spec->id == QLatin1String("unread_scan_band")) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QProgressDialog bandPreviewProgress(
                QStringLiteral("正在生成未读扫描带预览，请等待..."),
                QString(),
                0,
                0,
                this);
            bandPreviewProgress.setWindowTitle(QStringLiteral("微信未读扫描带"));
            bandPreviewProgress.setCancelButton(nullptr);
            bandPreviewProgress.setWindowModality(Qt::ApplicationModal);
            bandPreviewProgress.setMinimumDuration(0);
            bandPreviewProgress.setAutoClose(false);
            bandPreviewProgress.setAutoReset(false);
            bandPreviewProgress.show();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

            QProcess proc;
            proc.setWorkingDirectory(QStringLiteral(PROJECT_ROOT_DIR));
            proc.setProcessChannelMode(QProcess::MergedChannels);
            QStringList bandPreviewArgs;
            bandPreviewArgs << QStringLiteral("python/rpa/preview_wechat_unread_band.py");
            proc.start(QStringLiteral("python"), bandPreviewArgs);

            if (!proc.waitForStarted(3000)) {
                bandPreviewProgress.close();
                QApplication::restoreOverrideCursor();
                QMessageBox::warning(
                    this,
                    QStringLiteral("微信未读扫描带"),
                    QStringLiteral("已保存未读扫描带校准，但无法启动预览脚本。\n请确认命令行中的 python 可用。"));
                return;
            }
            if (!proc.waitForFinished(30000)) {
                proc.kill();
                proc.waitForFinished(1000);
                bandPreviewProgress.close();
                QApplication::restoreOverrideCursor();
                QMessageBox::warning(
                    this,
                    QStringLiteral("微信未读扫描带"),
                    QStringLiteral("已保存未读扫描带校准，但预览生成超时。"));
                return;
            }
            bandPreviewProgress.close();
            QApplication::restoreOverrideCursor();

            const QString output = QString::fromUtf8(proc.readAllStandardOutput());
            const QString prefix = QStringLiteral("UNREAD_BAND_PREVIEW_JSON=");
            const int pos = output.lastIndexOf(prefix);
            if (pos < 0) {
                QMessageBox::information(
                    this,
                    QStringLiteral("微信未读扫描带"),
                    QStringLiteral(
                        "已保存未读扫描带校准。\n"
                        "后续运行 Reader 时，会按新的扫描带检测红点，并在 python/rpa/_debug/wechat "
                        "中输出 unread 调试截图。"));
                return;
            }

            QJsonParseError err{};
            const QByteArray jsonBytes = output.mid(pos + prefix.size()).trimmed().toUtf8();
            const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
            if (!doc.isObject()) {
                QMessageBox::information(
                    this,
                    QStringLiteral("微信未读扫描带"),
                    QStringLiteral(
                        "已保存未读扫描带校准。\n"
                        "但未读扫描带预览解析失败：%1").arg(err.errorString()));
                return;
            }

            const QJsonObject preview = doc.object();
            const bool previewOk = preview.value(QStringLiteral("ok")).toBool();
            const QString listPath = preview.value(QStringLiteral("list_image_path")).toString();
            const QString bandPath = preview.value(QStringLiteral("band_image_path")).toString();
            const QString overlayHint = preview.value(QStringLiteral("overlay_hint")).toString();
            const QString errorText = preview.value(QStringLiteral("error")).toString();
            const QString msg = previewOk
                ? QStringLiteral(
                      "已保存未读扫描带校准。\n\n"
                      "扫描带：%1\n"
                      "会话列表截图：%2\n"
                      "扫描带截图：%3\n\n"
                      "后续运行 Reader 时，还会在 python/rpa/_debug/wechat 中继续输出 unread 调试截图。")
                      .arg(overlayHint.isEmpty() ? QStringLiteral("（未提供）") : overlayHint,
                           listPath.isEmpty() ? QStringLiteral("（未保存）") : listPath,
                           bandPath.isEmpty() ? QStringLiteral("（未保存）") : bandPath)
                : QStringLiteral(
                      "已保存未读扫描带校准，但即时预览失败：%1").arg(
                          errorText.isEmpty() ? QStringLiteral("未知错误") : errorText);
            QMessageBox::information(this, QStringLiteral("微信未读扫描带"), msg);
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        QProgressDialog previewProgress(
            QStringLiteral("正在进行即时 OCR 预览验证，请等待...\n首次加载 OCR 或窗口截图可能需要几秒。"),
            QString(),
            0,
            0,
            this);
        previewProgress.setWindowTitle(QStringLiteral("微信 OCR 预览"));
        previewProgress.setCancelButton(nullptr);
        previewProgress.setWindowModality(Qt::ApplicationModal);
        previewProgress.setMinimumDuration(0);
        previewProgress.setAutoClose(false);
        previewProgress.setAutoReset(false);
        previewProgress.show();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QProcess proc;
        proc.setWorkingDirectory(QStringLiteral(PROJECT_ROOT_DIR));
        proc.setProcessChannelMode(QProcess::MergedChannels);
        QStringList previewArgs;
        previewArgs << QStringLiteral("python/rpa/preview_wechat_region_ocr.py") << spec->id;
        proc.start(QStringLiteral("python"), previewArgs);
        if (!proc.waitForStarted(3000)) {
            previewProgress.close();
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(
                this,
                QStringLiteral("微信 OCR 预览"),
                QStringLiteral("已保存校准，但无法启动 Python 进行 OCR 预览。\n请确认命令行中的 python 可用。"));
            return;
        }
        if (!proc.waitForFinished(90000)) {
            proc.kill();
            proc.waitForFinished(1000);
            previewProgress.close();
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(
                this,
                QStringLiteral("微信 OCR 预览"),
                QStringLiteral("已保存校准，但 OCR 预览超时。\n可稍后查看 python/rpa/_debug/wechat 中的截图。"));
            return;
        }
        previewProgress.close();
        QApplication::restoreOverrideCursor();

        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const QString prefix = QStringLiteral("OCR_PREVIEW_JSON=");
        const int pos = output.lastIndexOf(prefix);
        if (pos < 0) {
            QMessageBox::warning(
                this,
                QStringLiteral("微信 OCR 预览"),
                QStringLiteral("已保存校准，但未解析到 OCR 预览结果。\n输出如下：\n%1")
                    .arg(output.trimmed().left(800)));
            return;
        }

        QJsonParseError err{};
        const QByteArray jsonBytes = output.mid(pos + prefix.size()).trimmed().toUtf8();
        const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
        if (!doc.isObject()) {
            QMessageBox::warning(
                this,
                QStringLiteral("微信 OCR 预览"),
                QStringLiteral("已保存校准，但 OCR 预览结果解析失败：%1").arg(err.errorString()));
            return;
        }

        const QJsonObject preview = doc.object();
        const bool previewOk = preview.value(QStringLiteral("ok")).toBool();
        const QString previewText = preview.value(QStringLiteral("text")).toString().trimmed();
        const QString imagePath = preview.value(QStringLiteral("image_path")).toString().trimmed();
        const QString errorText = preview.value(QStringLiteral("error")).toString().trimmed();
        const QString msg = previewOk
            ? QStringLiteral("区域：%1\nOCR 预览：%2\n截图：%3\n\n说明：OCR 内容仅用于快速验证区域是否命中，可能出现乱码或误识别。")
                  .arg(spec->label,
                       previewText.isEmpty() ? QStringLiteral("（未识别到文本）") : previewText,
                       imagePath.isEmpty() ? QStringLiteral("（未保存）") : imagePath)
            : QStringLiteral("区域：%1\nOCR 预览失败：%2\n截图：%3")
                  .arg(spec->label,
                       errorText.isEmpty() ? QStringLiteral("未知错误") : errorText,
                       imagePath.isEmpty() ? QStringLiteral("（未保存）") : imagePath);
        QMessageBox::information(this, QStringLiteral("微信 OCR 预览"), msg);
    });
}

quintptr MainWindow::findWechatCalibrationWindow() const
{
    quintptr bestHandle = 0;
    int bestArea = 0;

    for (auto it = m_managedWindows.constBegin(); it != m_managedWindows.constEnd(); ++it) {
        const auto& entry = it.value();
        if (!Win32WindowHelper::isWindowValid(entry.handle))
            continue;
        WindowInfo info;
        info.platformName = entry.platformName;
        info.processName = Win32WindowHelper::executableBaseNameForWindow(entry.handle);
        if (!isWechatWindowInfo(info))
            continue;
        const QRect wr = Win32WindowHelper::windowRect(entry.handle);
        const int area = wr.width() * wr.height();
        if (area > bestArea) {
            bestArea = area;
            bestHandle = entry.handle;
        }
    }

    if (bestHandle)
        return bestHandle;

    const QVector<WindowInfo> windows = Win32WindowHelper::enumTopLevelWindows();
    for (const auto& info : windows) {
        if (!isWechatWindowInfo(info))
            continue;
        const QRect wr = Win32WindowHelper::windowRect(info.handle);
        const int area = wr.width() * wr.height();
        if (area > bestArea) {
            bestArea = area;
            bestHandle = info.handle;
        }
    }
    return bestHandle;
}

bool MainWindow::mergeWriteWechatRpaConfig(quintptr hwnd,
                                           const QString& regionId,
                                           const QRect& regionRectWindowPx) const
{
    const QString path = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python/rpa/config/wechat_config.json");

    QJsonObject root;
    {
        QFile fin(path);
        if (fin.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray raw = fin.readAll();
            fin.close();
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (doc.isObject()) {
                root = doc.object();
            } else if (err.error != QJsonParseError::NoError) {
                qWarning() << "[MainWindow] wechat_config.json 解析失败，将仅保留合并字段:" << err.errorString();
            }
        }
    }

    root.insert(QStringLiteral("platform"), QStringLiteral("wechat_pc"));
    if (!root.contains(QStringLiteral("poll_interval_sec")))
        root.insert(QStringLiteral("poll_interval_sec"), 2);
    root.insert(QStringLiteral("hwnd_hex"),
                QStringLiteral("0x%1").arg(QString::number(static_cast<qulonglong>(hwnd), 16)));

    QJsonObject windowMatch = root.value(QStringLiteral("window_match")).toObject();
    if (!windowMatch.contains(QStringLiteral("process_name")))
        windowMatch.insert(QStringLiteral("process_name"), QStringLiteral("Weixin.exe"));
    if (!windowMatch.contains(QStringLiteral("title_contains")))
        windowMatch.insert(QStringLiteral("title_contains"), QStringLiteral("微信"));
    root.insert(QStringLiteral("window_match"), windowMatch);

    const QRect wr = Win32WindowHelper::windowRect(hwnd);
    if (wr.isValid()) {
        QJsonObject windowSize = root.value(QStringLiteral("window_size_px")).toObject();
        windowSize.insert(QStringLiteral("w"), wr.width());
        windowSize.insert(QStringLiteral("h"), wr.height());
        root.insert(QStringLiteral("window_size_px"), windowSize);
    }

    if (regionId == QLatin1String("chat_region")) {
        QJsonObject chatRegion = root.value(QStringLiteral("chat_region")).toObject();
        chatRegion.insert(QStringLiteral("mode"), QStringLiteral("relative_to_window"));
        chatRegion.insert(QStringLiteral("x"), regionRectWindowPx.x());
        chatRegion.insert(QStringLiteral("y"), regionRectWindowPx.y());
        chatRegion.insert(QStringLiteral("w"), regionRectWindowPx.width());
        chatRegion.insert(QStringLiteral("h"), regionRectWindowPx.height());
        root.insert(QStringLiteral("chat_region"), chatRegion);
    } else if (regionId == QLatin1String("input_box")) {
        QJsonObject inputBox = root.value(QStringLiteral("input_box")).toObject();
        inputBox.insert(QStringLiteral("x"), regionRectWindowPx.x());
        inputBox.insert(QStringLiteral("y"), regionRectWindowPx.y());
        inputBox.insert(QStringLiteral("w"), regionRectWindowPx.width());
        inputBox.insert(QStringLiteral("h"), regionRectWindowPx.height());
        root.insert(QStringLiteral("input_box"), inputBox);
    } else if (regionId == QLatin1String("contact_header_region")) {
        QJsonObject headerRegion = root.value(QStringLiteral("contact_header_region")).toObject();
        headerRegion.insert(QStringLiteral("x"), regionRectWindowPx.x());
        headerRegion.insert(QStringLiteral("y"), regionRectWindowPx.y());
        headerRegion.insert(QStringLiteral("w"), regionRectWindowPx.width());
        headerRegion.insert(QStringLiteral("h"), regionRectWindowPx.height());
        root.insert(QStringLiteral("contact_header_region"), headerRegion);
    } else if (regionId == QLatin1String("conversation_list_region")) {
        QJsonObject listRegion = root.value(QStringLiteral("conversation_list_region")).toObject();
        listRegion.insert(QStringLiteral("x"), regionRectWindowPx.x());
        listRegion.insert(QStringLiteral("y"), regionRectWindowPx.y());
        listRegion.insert(QStringLiteral("w"), regionRectWindowPx.width());
        listRegion.insert(QStringLiteral("h"), regionRectWindowPx.height());
        if (!listRegion.contains(QStringLiteral("row_height_guess")))
            listRegion.insert(QStringLiteral("row_height_guess"), 65);
        root.insert(QStringLiteral("conversation_list_region"), listRegion);
    } else if (regionId == QLatin1String("search_box")) {
        QJsonObject searchBox = root.value(QStringLiteral("search_box")).toObject();
        const int centerX = regionRectWindowPx.x() + regionRectWindowPx.width() / 2;
        const int centerY = regionRectWindowPx.y() + regionRectWindowPx.height() / 2;
        searchBox.insert(QStringLiteral("x"), centerX);
        searchBox.insert(QStringLiteral("y"), centerY);
        searchBox.insert(QStringLiteral("w"), regionRectWindowPx.width());
        searchBox.insert(QStringLiteral("h"), regionRectWindowPx.height());
        searchBox.insert(QStringLiteral("ocr_x"), regionRectWindowPx.x());
        searchBox.insert(QStringLiteral("ocr_y"), regionRectWindowPx.y());
        searchBox.insert(QStringLiteral("ocr_w"), regionRectWindowPx.width());
        searchBox.insert(QStringLiteral("ocr_h"), regionRectWindowPx.height());
        root.insert(QStringLiteral("search_box"), searchBox);
    } else if (regionId == QLatin1String("search_result_region")) {
        QJsonObject searchBox = root.value(QStringLiteral("search_box")).toObject();
        const int centerX = regionRectWindowPx.x() + regionRectWindowPx.width() / 2;
        const int centerY = regionRectWindowPx.y() + regionRectWindowPx.height() / 2;
        searchBox.insert(QStringLiteral("first_result_x"), centerX);
        searchBox.insert(QStringLiteral("first_result_y"), centerY);
        searchBox.insert(QStringLiteral("result_x"), regionRectWindowPx.x());
        searchBox.insert(QStringLiteral("result_y"), regionRectWindowPx.y());
        searchBox.insert(QStringLiteral("result_w"), regionRectWindowPx.width());
        searchBox.insert(QStringLiteral("result_h"), regionRectWindowPx.height());
        root.insert(QStringLiteral("search_box"), searchBox);
    } else if (regionId == QLatin1String("unread_scan_band")) {
        const QJsonObject listRegion = root.value(QStringLiteral("conversation_list_region")).toObject();
        const int listX = listRegion.value(QStringLiteral("x")).toInt();
        const int listW = listRegion.value(QStringLiteral("w")).toInt();
        if (listW <= 0) {
            qWarning() << "[MainWindow] 未读扫描带校准失败：conversation_list_region 未配置有效宽度";
            return false;
        }
        const int localLeft = qBound(0, regionRectWindowPx.x() - listX, listW - 1);
        const int localRight = qBound(localLeft + 1, regionRectWindowPx.x() + regionRectWindowPx.width() - listX, listW);
        const double startRatio = double(localLeft) / double(listW);
        const double endRatio = double(localRight) / double(listW);

        QJsonObject unread = root.value(QStringLiteral("unread_detection")).toObject();
        unread.insert(QStringLiteral("scan_x_start_ratio"), startRatio);
        unread.insert(QStringLiteral("scan_x_end_ratio"), endRatio);
        unread.insert(QStringLiteral("scan_band_comment"),
                      QStringLiteral("由微信OCR校准的『未读扫描带』生成；相对 conversation_list_region 宽度的比例"));
        root.insert(QStringLiteral("unread_detection"), unread);

        qInfo() << "[MainWindow] 微信未读扫描带已保存"
                << "listX=" << listX
                << "listW=" << listW
                << "local=[" << localLeft << "," << localRight << ")"
                << "ratio=[" << startRatio << "," << endRatio << ")";
    } else {
        qWarning() << "[MainWindow] 未知微信校准区域:" << regionId;
        return false;
    }

    QFile fout(path);
    if (!fout.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[MainWindow] 写入 wechat_config.json 失败:" << path << fout.errorString();
        return false;
    }
    fout.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    fout.close();
    return true;
}

void MainWindow::startQianniuRpaCalibration(const QString& platformId)
{
    if (!m_managedWindows.contains(platformId))
        return;

    const ManagedWindowEntry entry = m_managedWindows.value(platformId);
    const quintptr hwnd = entry.handle;
    if (!Win32WindowHelper::isWindowValid(hwnd)) {
        qWarning() << "[MainWindow] 千牛RPA校准失败：窗口无效";
        return;
    }

    QMessageBox::information(
        this,
        QStringLiteral("千牛 OCR 区域校准"),
        QStringLiteral(
            "将分两步在全屏半透明层上框选（可隐约看到下层千牛）。\n"
            "第 1 步：中间消息气泡滚动区；第 2 步：顶栏店名/会话标题（尽量避开纯 KPI 数据条）。\n"
            "每步：拖拽框选后按回车确认，Esc 取消。\n\n"
            "【窗口大小】校准保存的是相对千牛窗口像素坐标。嵌入模式下千牛会随主窗口缩放，"
            "若之后明显改变主窗口大小或布局，需重新校准。\n"
            "建议：在与日常使用时相同的主窗口大小下校准（例如您习惯最大化，就先最大化再校准）。\n\n"
            "请先把「接待中心」放在包含该窗口中心的显示器上并保持可见。"));

    const QRect wr = Win32WindowHelper::windowRect(hwnd);
    QScreen* baseScreen = QGuiApplication::screenAt(wr.center());
    if (!baseScreen)
        baseScreen = QGuiApplication::primaryScreen();
    const QRect screenGeom = baseScreen->geometry();

    auto* o1 = new RpaRegionCalibrationOverlay(nullptr);
    o1->setAttribute(Qt::WA_NativeWindow, true);
    o1->setHelpTip(QStringLiteral(
        "千牛 1/2：框选中间聊天气泡区（与 Reader 截图一致），Esc 取消，回车确认"));
    o1->setDimColor(QColor(24, 26, 32, 72));
    o1->setGeometry(screenGeom);
    o1->show();
    o1->raise();
    o1->activateWindow();
    o1->setFocus();

    qInfo() << "[MainWindow] 千牛 OCR 校准开始 handle=0x"
            << QString::number(static_cast<qulonglong>(hwnd), 16)
            << "screenGeom(" << formatRect(screenGeom) << ")";

    o1->setOnFinished([this, hwnd, screenGeom, o1](bool ok, QRect sel) {
        if (!ok) {
            qInfo() << "[MainWindow] 千牛校准第 1 步已取消";
            return;
        }
        if (!Win32WindowHelper::isWindowValid(hwnd)) {
            qWarning() << "[MainWindow] 千牛校准失败：窗口已无效";
            return;
        }
        QScreen* sc = o1->screen();
        const qreal dpr = sc ? sc->devicePixelRatio() : 1.0;
        const QRect chatPx = Win32WindowHelper::mapOverlaySelectionToTargetWindowRelative(
            o1->winId(), hwnd, sel, dpr, o1->size());
        if (chatPx.width() < 16 || chatPx.height() < 16) {
            qWarning() << "[MainWindow] 千牛校准第1步映射失败 sel=" << formatRect(sel)
                       << "overlaySize=" << o1->width() << "x" << o1->height()
                       << "dpr=" << dpr << "mapped=" << formatRect(chatPx);
            QMessageBox::warning(
                this,
                QStringLiteral("千牛校准"),
                QStringLiteral("第 1 步选区未能正确映射到千牛窗口（常见于 DPI/全屏遮罩坐标不一致）。\n"
                               "请重试；若仍失败，请暂时将千牛从主程序「断开关联」后以独立窗口校准。"));
            return;
        }
        qInfo() << "[MainWindow] 千牛校准消息区(窗相对px):" << formatRect(chatPx) << "dpr=" << dpr;

        auto* o2 = new RpaRegionCalibrationOverlay(nullptr);
        o2->setAttribute(Qt::WA_NativeWindow, true);
        o2->setHelpTip(QStringLiteral(
            "千牛 2/2：框选顶栏店名/会话标题（避开「昨日响应率」等单行 KPI），Esc 取消，回车确认"));
        o2->setDimColor(QColor(24, 26, 32, 72));
        o2->setGeometry(screenGeom);
        o2->show();
        o2->raise();
        o2->activateWindow();
        o2->setFocus();

        o2->setOnFinished([this, hwnd, chatPx, o2](bool ok2, QRect sel2) {
            if (!ok2) {
                qInfo() << "[MainWindow] 千牛校准第 2 步已取消（配置未写入）";
                return;
            }
            if (!Win32WindowHelper::isWindowValid(hwnd)) {
                qWarning() << "[MainWindow] 千牛校准写入失败：窗口已无效";
                return;
            }
            QScreen* sc2 = o2->screen();
            const qreal dpr2 = sc2 ? sc2->devicePixelRatio() : 1.0;
            const QRect headerPx = Win32WindowHelper::mapOverlaySelectionToTargetWindowRelative(
                o2->winId(), hwnd, sel2, dpr2, o2->size());
            if (headerPx.width() < 8 || headerPx.height() < 8) {
                qWarning() << "[MainWindow] 千牛校准第2步映射失败 sel=" << formatRect(sel2)
                           << "mapped=" << formatRect(headerPx);
                QMessageBox::warning(this,
                                     QStringLiteral("千牛校准"),
                                     QStringLiteral("标题区域映射失败或过小，请重试。"));
                return;
            }
            const bool saved = mergeWriteQianniuRpaConfig(hwnd, chatPx, headerPx);
            statusBar()->showMessage(saved ? QStringLiteral("已写入 python/rpa/config/qianniu_config.json（含 x,y,w,h）")
                                           : QStringLiteral("千牛配置写入失败，请查看日志"),
                                     5000);
            qInfo() << "[MainWindow] 千牛校准标题区:" << formatRect(headerPx) << "saved=" << saved;
        });
    });
}

bool MainWindow::mergeWriteQianniuRpaConfig(quintptr hwnd,
                                          const QRect& chatRectWindowPx,
                                          const QRect& headerRectWindowPx) const
{
    const QString path = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python/rpa/config/qianniu_config.json");

    QJsonObject root;
    {
        QFile fin(path);
        if (fin.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray raw = fin.readAll();
            fin.close();
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (doc.isObject()) {
                root = doc.object();
            } else if (err.error != QJsonParseError::NoError) {
                qWarning() << "[MainWindow] qianniu_config.json 解析失败，将仅保留合并字段:" << err.errorString();
            }
        }
    }

    root.insert(QStringLiteral("hwnd_hex"),
                QStringLiteral("0x%1").arg(QString::number(static_cast<qulonglong>(hwnd), 16)));

    QJsonObject chatRegion = root.value(QStringLiteral("chat_region")).toObject();
    chatRegion.insert(QStringLiteral("mode"), QStringLiteral("relative_to_window"));
    chatRegion.insert(QStringLiteral("xywh_comment"),
                      QStringLiteral("x,y,w,h 相对整窗 PrintWindow 左上角；存在时 Python 优先于比例字段"));
    chatRegion.insert(QStringLiteral("x"), chatRectWindowPx.x());
    chatRegion.insert(QStringLiteral("y"), chatRectWindowPx.y());
    chatRegion.insert(QStringLiteral("w"), chatRectWindowPx.width());
    chatRegion.insert(QStringLiteral("h"), chatRectWindowPx.height());
    root.insert(QStringLiteral("chat_region"), chatRegion);

    QJsonObject hdr = root.value(QStringLiteral("contact_header_region")).toObject();
    hdr.insert(QStringLiteral("xywh_comment"),
               QStringLiteral("x,y,w,h 相对整窗 PrintWindow；存在时 Python 优先于比例"));
    hdr.insert(QStringLiteral("x"), headerRectWindowPx.x());
    hdr.insert(QStringLiteral("y"), headerRectWindowPx.y());
    hdr.insert(QStringLiteral("w"), headerRectWindowPx.width());
    hdr.insert(QStringLiteral("h"), headerRectWindowPx.height());
    root.insert(QStringLiteral("contact_header_region"), hdr);

    QFile fout(path);
    if (!fout.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[MainWindow] 写入 qianniu_config.json 失败:" << path << fout.errorString();
        return false;
    }
    fout.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    fout.close();
    return true;
}

void MainWindow::startPddRpaCalibration(const QString& platformId)
{
    if (!m_managedWindows.contains(platformId))
        return;

    const ManagedWindowEntry entry = m_managedWindows.value(platformId);
    const quintptr hwnd = entry.handle;
    if (!Win32WindowHelper::isWindowValid(hwnd)) {
        qWarning() << "[MainWindow] 拼多多RPA校准失败：窗口无效";
        return;
    }

    // Ensure we are showing the correct page so user can see selection area
    switchToWindow(platformId);

    QMessageBox::information(
        this,
        QStringLiteral("拼多多 OCR 区域校准"),
        QStringLiteral(
            "将分两步在全屏半透明层上框选（可隐约看到下层拼多多后台）。\n"
            "第 1 步：聊天消息气泡滚动区；第 2 步：输入框区域（用于点击聚焦+粘贴）。\n"
            "每步：拖拽框选后按回车确认，Esc 取消。\n\n"
            "提示：校准保存的是相对拼多多窗口 PrintWindow 像素坐标；请尽量保持浏览器缩放为 100%。"));

    const QRect wr = Win32WindowHelper::windowRect(hwnd);
    QScreen* baseScreen = QGuiApplication::screenAt(wr.center());
    if (!baseScreen)
        baseScreen = QGuiApplication::primaryScreen();
    const QRect screenGeom = baseScreen->geometry();

    auto* o1 = new RpaRegionCalibrationOverlay(nullptr);
    o1->setAttribute(Qt::WA_NativeWindow, true);
    o1->setHelpTip(QStringLiteral("拼多多 1/2：框选聊天消息区域，Esc 取消，回车确认"));
    o1->setDimColor(QColor(24, 26, 32, 72));
    o1->setGeometry(screenGeom);
    o1->show();
    o1->raise();
    o1->activateWindow();
    o1->setFocus();

    qInfo() << "[MainWindow] 拼多多 OCR 校准开始 handle=0x"
            << QString::number(static_cast<qulonglong>(hwnd), 16)
            << "screenGeom(" << formatRect(screenGeom) << ")";

    o1->setOnFinished([this, hwnd, screenGeom, o1](bool ok, QRect sel) {
        if (!ok) {
            qInfo() << "[MainWindow] 拼多多校准第 1 步已取消";
            return;
        }
        if (!Win32WindowHelper::isWindowValid(hwnd)) {
            qWarning() << "[MainWindow] 拼多多校准第 1 步失败：窗口已无效";
            return;
        }

        QScreen* sc = o1->screen();
        const qreal dpr = sc ? sc->devicePixelRatio() : 1.0;
        const QRect chatPx = Win32WindowHelper::mapOverlaySelectionToTargetWindowRelative(
            o1->winId(), hwnd, sel, dpr, o1->size());

        if (chatPx.width() < 16 || chatPx.height() < 16) {
            qWarning() << "[MainWindow] 拼多多校准第1步映射失败 sel=" << formatRect(sel)
                       << "mapped=" << formatRect(chatPx);
            QMessageBox::warning(this,
                                 QStringLiteral("拼多多校准"),
                                 QStringLiteral("第 1 步选区映射失败或过小，请重试。"));
            return;
        }

        auto* o2 = new RpaRegionCalibrationOverlay(nullptr);
        o2->setAttribute(Qt::WA_NativeWindow, true);
        o2->setHelpTip(QStringLiteral("拼多多 2/2：框选输入框区域，Esc 取消，回车确认"));
        o2->setDimColor(QColor(24, 26, 32, 72));
        o2->setGeometry(screenGeom);
        o2->show();
        o2->raise();
        o2->activateWindow();
        o2->setFocus();

        o2->setOnFinished([this, hwnd, chatPx, o2](bool ok2, QRect sel2) {
            if (!ok2) {
                qInfo() << "[MainWindow] 拼多多校准第 2 步已取消（配置未写入）";
                return;
            }
            if (!Win32WindowHelper::isWindowValid(hwnd)) {
                qWarning() << "[MainWindow] 拼多多校准写入失败：窗口已无效";
                return;
            }

            QScreen* sc2 = o2->screen();
            const qreal dpr2 = sc2 ? sc2->devicePixelRatio() : 1.0;
            const QRect inputPx = Win32WindowHelper::mapOverlaySelectionToTargetWindowRelative(
                o2->winId(), hwnd, sel2, dpr2, o2->size());

            if (inputPx.width() < 8 || inputPx.height() < 8) {
                qWarning() << "[MainWindow] 拼多多校准第2步映射失败 sel=" << formatRect(sel2)
                           << "mapped=" << formatRect(inputPx);
                QMessageBox::warning(this,
                                     QStringLiteral("拼多多校准"),
                                     QStringLiteral("第 2 步选区映射失败或过小，请重试。"));
                return;
            }

            const bool saved = mergeWritePddRpaConfig(hwnd, chatPx, inputPx);
            statusBar()->showMessage(saved ? QStringLiteral("已写入 python/rpa/config/pdd_config.json（含 chat/input x,y,w,h）")
                                           : QStringLiteral("拼多多配置写入失败，请查看日志"),
                                     5000);
            qInfo() << "[MainWindow] 拼多多校准结果 chat=" << formatRect(chatPx)
                    << "input=" << formatRect(inputPx)
                    << "saved=" << saved;
        });
    });
}

bool MainWindow::mergeWritePddRpaConfig(quintptr hwnd,
                                        const QRect& chatRectWindowPx,
                                        const QRect& inputRectWindowPx) const
{
    const QString path = QStringLiteral(PROJECT_ROOT_DIR) + QStringLiteral("/python/rpa/config/pdd_config.json");

    QJsonObject root;
    {
        QFile fin(path);
        if (fin.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray raw = fin.readAll();
            fin.close();
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (doc.isObject()) {
                root = doc.object();
            } else if (err.error != QJsonParseError::NoError) {
                qWarning() << "[MainWindow] pdd_config.json 解析失败，将仅保留合并字段:" << err.errorString();
            }
        }
    }

    root.insert(QStringLiteral("hwnd_hex"),
                QStringLiteral("0x%1").arg(QString::number(static_cast<qulonglong>(hwnd), 16)));

    QJsonObject chatRegion = root.value(QStringLiteral("chat_region")).toObject();
    chatRegion.insert(QStringLiteral("coordinates"), QStringLiteral("window"));
    chatRegion.insert(QStringLiteral("xywh_comment"),
                       QStringLiteral("x,y,w,h 相对整窗 PrintWindow 左上角；存在时 Python 优先于比例字段"));
    chatRegion.insert(QStringLiteral("x"), chatRectWindowPx.x());
    chatRegion.insert(QStringLiteral("y"), chatRectWindowPx.y());
    chatRegion.insert(QStringLiteral("w"), chatRectWindowPx.width());
    chatRegion.insert(QStringLiteral("h"), chatRectWindowPx.height());
    root.insert(QStringLiteral("chat_region"), chatRegion);

    QJsonObject inputRegion = root.value(QStringLiteral("input_region")).toObject();
    inputRegion.insert(QStringLiteral("coordinates"), QStringLiteral("window"));
    inputRegion.insert(QStringLiteral("xywh_comment"),
                        QStringLiteral("x,y,w,h 相对整窗 PrintWindow 左上角；用于点击输入框聚焦"));
    inputRegion.insert(QStringLiteral("x"), inputRectWindowPx.x());
    inputRegion.insert(QStringLiteral("y"), inputRectWindowPx.y());
    inputRegion.insert(QStringLiteral("w"), inputRectWindowPx.width());
    inputRegion.insert(QStringLiteral("h"), inputRectWindowPx.height());
    root.insert(QStringLiteral("input_region"), inputRegion);

    QFile fout(path);
    if (!fout.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[MainWindow] 写入 pdd_config.json 失败:" << path << fout.errorString();
        return false;
    }
    fout.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    fout.close();
    return true;
}

// ==================== Aggregate Chat ====================

void MainWindow::openAggregateChatForm()
{
    if (!m_aggregateChatForm) {
        m_aggregateChatForm = new AggregateChatForm(m_username, this);
        m_centerStack->addWidget(m_aggregateChatForm);
    }
    m_centerStack->setCurrentWidget(m_aggregateChatForm);
}

// ==================== EmbeddedWindowContainer ====================

EmbeddedWindowContainer::EmbeddedWindowContainer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
}

void EmbeddedWindowContainer::setEmbeddedHandle(quintptr handle)
{
    m_handle = handle;
    if (m_handle) {
        Win32WindowHelper::embedWindowIntoWidget(m_handle, this);
        Win32WindowHelper::resizeEmbeddedWindow(m_handle, this);
    }
}

quintptr EmbeddedWindowContainer::embeddedHandle() const
{
    return m_handle;
}

void EmbeddedWindowContainer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_handle) {
        Win32WindowHelper::resizeEmbeddedWindow(m_handle, this);
    }
}

// ==================== Window Events ====================

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (m_alwaysOnTop)
        Win32WindowHelper::applyNativeTopMost(this, true);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    SwordCursor::restore();
    detachAllWindows();
    const QStringList keys = m_rpaProcesses.keys();
    for (const QString& id : keys) {
        QProcess* proc = m_rpaProcesses.take(id);
        if (!proc)
            continue;
        proc->disconnect();
        proc->kill();
        proc->waitForFinished(1500);
        proc->deleteLater();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    updateFloatFollowPosition();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateFloatFollowPosition();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
            hideCurrentFloatWindow();
        } else {
            QTimer::singleShot(100, this, &MainWindow::updateFloatFollowPosition);
        }
    } else if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) {
            QTimer::singleShot(50, this, &MainWindow::updateFloatFollowPosition);
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_statusMessage) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                refreshStatusMessage();
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            auto* contextEvent = static_cast<QContextMenuEvent*>(event);
            QMenu menu(this);
            QAction* actManage = menu.addAction(QStringLiteral("管理文案"));
            QAction* chosen = menu.exec(contextEvent->globalPos());
            if (chosen == actManage) {
                openStatusMessageManager();
            }
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshStatusMessage()
{
    if (!m_statusMessage)
        return;

    const QStringList messages = allEncouragementMessages(m_customStatusMessages);
    m_statusMessage->setText(randomEncouragementText(messages, m_statusMessage->text()));
}

void MainWindow::openStatusMessageManager()
{
    const ApplyStyle::MainWindowTheme theme = m_mainWindowTheme;
    QDialog dialog(this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setObjectName(QStringLiteral("statusMessageManagerDialog"));
    dialog.setWindowTitle(QStringLiteral("管理文案"));
    dialog.setModal(true);
    dialog.resize(450, 320);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* tipLabel = new QLabel(QStringLiteral("这里管理自定义文案；内置文案仍会默认参与随机显示。"), &dialog);
    tipLabel->setWordWrap(true);

    auto* listWidget = new QListWidget(&dialog);
    listWidget->setObjectName(QStringLiteral("statusMessageList"));
    listWidget->addItems(m_customStatusMessages);

    auto* editor = new QLineEdit(&dialog);
    editor->setObjectName(QStringLiteral("statusMessageEditor"));
    editor->setPlaceholderText(QStringLiteral("输入一句想展示的话"));

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    auto* addButton = new QPushButton(QStringLiteral("新增"), &dialog);
    auto* updateButton = new QPushButton(QStringLiteral("修改"), &dialog);
    auto* deleteButton = new QPushButton(QStringLiteral("删除"), &dialog);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), &dialog);
    addButton->setObjectName(QStringLiteral("statusMessageAddButton"));
    updateButton->setObjectName(QStringLiteral("statusMessageUpdateButton"));
    deleteButton->setObjectName(QStringLiteral("statusMessageDeleteButton"));
    closeButton->setObjectName(QStringLiteral("statusMessageCloseButton"));
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(updateButton);
    buttonRow->addWidget(deleteButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(closeButton);

    layout->addWidget(tipLabel);
    layout->addWidget(listWidget, 1);
    layout->addWidget(editor);
    layout->addLayout(buttonRow);

    auto syncCustomMessages = [this, listWidget]() {
        QStringList messages;
        for (int i = 0; i < listWidget->count(); ++i)
            messages.append(listWidget->item(i)->text());
        m_customStatusMessages = normalizedMessages(messages);
        saveCustomEncouragementMessages(m_customStatusMessages);
        refreshStatusMessage();
    };

    auto findRowByText = [listWidget](const QString& text, int ignoreRow = -1) {
        for (int i = 0; i < listWidget->count(); ++i) {
            if (i != ignoreRow && listWidget->item(i)->text() == text)
                return i;
        }
        return -1;
    };

    connect(listWidget, &QListWidget::currentTextChanged, &dialog, [editor](const QString& text) {
        editor->setText(text);
        editor->selectAll();
    });
    connect(addButton, &QPushButton::clicked, &dialog, [=]() {
        const QString text = editor->text().trimmed();
        if (text.isEmpty()) {
            editor->setFocus();
            return;
        }

        const int existingRow = findRowByText(text);
        if (existingRow >= 0) {
            listWidget->setCurrentRow(existingRow);
            editor->setFocus();
            return;
        }

        listWidget->addItem(text);
        listWidget->setCurrentRow(listWidget->count() - 1);
        syncCustomMessages();
        editor->clear();
        editor->setFocus();
    });
    connect(updateButton, &QPushButton::clicked, &dialog, [=]() {
        auto* currentItem = listWidget->currentItem();
        if (!currentItem) {
            editor->setFocus();
            return;
        }

        const QString text = editor->text().trimmed();
        if (text.isEmpty()) {
            editor->setFocus();
            return;
        }

        const int currentRow = listWidget->row(currentItem);
        const int existingRow = findRowByText(text, currentRow);
        if (existingRow >= 0) {
            listWidget->setCurrentRow(existingRow);
            editor->setFocus();
            return;
        }

        currentItem->setText(text);
        syncCustomMessages();
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [=]() {
        auto* currentItem = listWidget->currentItem();
        if (!currentItem)
            return;

        delete listWidget->takeItem(listWidget->row(currentItem));
        syncCustomMessages();
        editor->clear();
        editor->setFocus();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(editor, &QLineEdit::returnPressed, &dialog, [=]() {
        if (listWidget->currentItem())
            updateButton->click();
        else
            addButton->click();
    });

    dialog.setStyleSheet(ApplyStyle::statusMessageManagerStyle(theme));
    dialog.adjustSize();

    if (m_statusMessage) {
        const QSize dialogSize = dialog.size();
        QPoint anchor = m_statusMessage->mapToGlobal(QPoint(m_statusMessage->width(), 0));
        QScreen* screen = QGuiApplication::screenAt(anchor);
        if (!screen && windowHandle())
            screen = windowHandle()->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();

        QRect available = screen ? screen->availableGeometry() : QRect();
        int x = anchor.x() - dialogSize.width();
        int y = anchor.y() - dialogSize.height() - 8;
        if (screen) {
            x = qBound(available.left(), x, available.right() - dialogSize.width());
            if (y < available.top())
                y = qMin(anchor.y() + m_statusMessage->height() + 8, available.bottom() - dialogSize.height());
        }
        dialog.move(x, y);
    }

    dialog.exec();
}

void MainWindow::buildStatusBar()
{
    m_customStatusMessages = loadCustomEncouragementMessages();
    m_btnThemeSwitch = new QToolButton(this);
    m_btnThemeSwitch->setObjectName(QStringLiteral("themeSwitchButton"));
    m_btnThemeSwitch->setText(QStringLiteral("主题"));
    m_btnThemeSwitch->setCursor(Qt::PointingHandCursor);
    m_btnThemeSwitch->setPopupMode(QToolButton::InstantPopup);
    m_btnThemeSwitch->setToolTip(QStringLiteral("默认 / 冷色 / 暖色"));
    auto* themeMenu = new QMenu(m_btnThemeSwitch);
    themeMenu->addAction(QStringLiteral("默认"), this, [this]() {
        applyMainWindowTheme(ApplyStyle::MainWindowTheme::Default);
    });
    themeMenu->addAction(QStringLiteral("冷色"), this, [this]() {
        applyMainWindowTheme(ApplyStyle::MainWindowTheme::Cool);
    });
    themeMenu->addAction(QStringLiteral("暖色"), this, [this]() {
        applyMainWindowTheme(ApplyStyle::MainWindowTheme::Warm);
    });
    m_btnThemeSwitch->setMenu(themeMenu);
    statusBar()->addWidget(m_btnThemeSwitch);

    auto* statusWrap = new QWidget(this);
    statusWrap->setObjectName("statusBarWrap");
    auto* statusLayout = new QHBoxLayout(statusWrap);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    m_statusMessage = new QLabel(statusWrap);
    m_statusSeparator = new QLabel(statusWrap);
    m_statusTime = new QLabel(statusWrap);
    m_statusMessage->setObjectName("statusMessage");
    m_statusSeparator->setObjectName("statusSeparator");
    m_statusTime->setObjectName("statusTime");
    m_statusMessage->setCursor(Qt::PointingHandCursor);
    m_statusMessage->setToolTip(QStringLiteral("左键换一句，右键管理文案"));
    m_statusSeparator->setText(QStringLiteral(" | "));
    m_statusMessage->installEventFilter(this);
    statusLayout->addWidget(m_statusMessage);
    statusLayout->addWidget(m_statusSeparator);
    statusLayout->addWidget(m_statusTime);
    statusBar()->addPermanentWidget(statusWrap);
    auto* timeTimer = new QTimer(this);
    connect(timeTimer, &QTimer::timeout, this, [this]() {
        m_statusTime->setText(QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy年MM月dd日 hh:mm:ss")));
    });
    timeTimer->start(1000);
    refreshStatusMessage();
    m_statusTime->setText(QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy年MM月dd日 hh:mm:ss")));
}
