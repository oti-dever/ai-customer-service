#include "mainwindow.h"
#include "addwindowdialog.h"
#include "aggregatechatform.h"
#include "robotassistantwidget.h"
#include "aicustomerservicebackendwindow.h"
#include "editprofiledialog.h"
#include "foldarrowcombobox.h"
#include "pythonserviceconnectiondialog.h"
#include "helpcenterdialog.h"
#include "../data/userdao.h"
#include "../utils/appsettings.h"
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
#include <QDialogButtonBox>
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
#include <QSpinBox>
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
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QDirIterator>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QUrl>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QWindow>
#include <functional>
#include <QFile>
#include <QRegularExpression>
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
    QSettings settings = AppSettings::create();
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
    QSettings settings = AppSettings::create();
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

// ==================== Tree roles & delegate ====================

namespace {
enum PlatformTreeRole {
    PlatformIdRole = Qt::UserRole,
    IsGroupRole,
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

/** 树行展示名：Display 可为空（纯图标）；占位页等回退到 tooltip。 */
QString platformTreeRowLabel(const QModelIndex& idx)
{
    const QString d = idx.data(Qt::DisplayRole).toString();
    if (!d.isEmpty())
        return d;
    return idx.data(Qt::ToolTipRole).toString();
}

/**
 * QTreeView 传给委托的 option.rect 往往在「分支/展开」占位右侧，宽度也只是内容列；
 * 在窄栏里按 rect 几何中心画图标会整体偏右甚至裁切。「在线平台」等分组行尤其明显。
 */
QRect platformTreeItemFullRowRect(const QStyleOptionViewItem& option)
{
    QRect rect = option.rect;
    const QWidget* w = option.widget;
    const QTreeView* tree = w ? qobject_cast<const QTreeView*>(w) : nullptr;
    const QTreeView* tv = tree ? tree : (w && w->parentWidget()
                                            ? qobject_cast<const QTreeView*>(w->parentWidget())
                                            : nullptr);
    if (tv) {
        const QWidget* vp = tv->viewport();
        const int vw = vp ? vp->width() : rect.width();
        rect.setX(0);
        rect.setWidth(vw);
    }
    return rect;
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

namespace {

/** 与配置弹窗同风格；确定则写入 *outValue 并返回 true，取消返回 false。 */
bool showStyledIntInputDialog(QWidget* parent,
                              ApplyStyle::MainWindowTheme theme,
                              const QString& windowTitle,
                              const QString& bodyText,
                              int initialValue,
                              int minV,
                              int maxV,
                              int step,
                              int* outValue)
{
    if (!outValue)
        return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(windowTitle);
    dlg.setModal(true);
    dlg.setWindowFlags((dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint) | Qt::Dialog);

    const QString disabledBorder = (theme == ApplyStyle::MainWindowTheme::Default)
        ? QStringLiteral("#D4D4D8")
        : QStringLiteral("#c0d9f7");
    dlg.setStyleSheet(ApplyStyle::addWindowDialogStyle(theme)
                      + QStringLiteral(
                            R"QSS(
        QDialog QPushButton:disabled {
            color: #1e293b;
            background-color: #ffffff;
            border: 1px solid %1;
            border-radius: 8px;
            padding: 6px 16px;
            min-height: 22px;
        }
    )QSS")
                            .arg(disabledBorder));
    dlg.setMinimumWidth(440);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* body = new QLabel(bodyText, &dlg);
    body->setWordWrap(true);

    auto* spin = new QSpinBox(&dlg);
    spin->setRange(minV, maxV);
    spin->setSingleStep(step);
    spin->setValue(qBound(minV, initialValue, maxV));
    spin->setMinimumWidth(140);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto* b = box->button(QDialogButtonBox::Ok))
        b->setText(QStringLiteral("确定"));
    if (auto* b = box->button(QDialogButtonBox::Cancel))
        b->setText(QStringLiteral("取消"));

    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    layout->addWidget(body);
    layout->addWidget(spin);
    layout->addStretch(1);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    *outValue = spin->value();
    return true;
}

} // namespace

QStringList loadCustomEncouragementMessages()
{
    QSettings settings = AppSettings::create();
    return normalizedMessages(settings.value(QStringLiteral("statusBar/customMessages")).toStringList());
}

void saveCustomEncouragementMessages(const QStringList& messages)
{
    QSettings settings = AppSettings::create();
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
    explicit PlatformTreeDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool isGroup = index.data(IsGroupRole).toBool();
        const QRect row = platformTreeItemFullRowRect(option);
        return {row.width(), isGroup ? 48 : 52};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const PlatformTreeColors c = ApplyStyle::platformTreeColors(ApplyStyle::MainWindowTheme::Default);

        const bool isGroup = index.data(IsGroupRole).toBool();
        const bool sel = (option.state & QStyle::State_Selected) != 0;
        const bool hover = (option.state & QStyle::State_MouseOver) != 0;

        const QRect rowRect = platformTreeItemFullRowRect(option);
        const int cellW = rowRect.width();
        const bool narrow = cellW <= 96;

        QRect r = narrow ? rowRect.adjusted(4, 4, -4, -4) : rowRect.adjusted(8, 4, -8, -4);

        if (isGroup) {
            QColor bg = c.groupBgDefault;
            if (hover) bg = c.groupBgHover;
            if (sel) bg = c.groupBgSelected;
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, narrow ? 6 : 8, narrow ? 6 : 8);

            QIcon groupIcon = index.data(Qt::DecorationRole).value<QIcon>();
            const int iconSz = 28;

            QRect iconRect(0, 0, iconSz, iconSz);
            iconRect.moveCenter(r.center());
            if (!groupIcon.isNull())
                groupIcon.paint(painter, iconRect, Qt::AlignCenter);
        } else {
            const QString title = index.data(Qt::DisplayRole).toString();
            const bool isCS = index.data(IsCustomerServiceItemRole).toBool();
            const bool isActivated = index.data(IsActivatedRole).toBool();

            QColor bg;
            if (sel) {
                bg = c.itemBgSelected;
            } else if (isCS && !isActivated) {
                bg = c.itemInactiveBgDefault;
                if (hover) bg = c.itemInactiveBgHover;
            } else {
                bg = c.itemBgDefault;
                if (hover) bg = c.itemBgHover;
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, narrow ? 6 : 8, narrow ? 6 : 8);

            QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            QSize iconSize(28, 28);
            QRect iconRect(0, 0, iconSize.width(), iconSize.height());
            if (narrow) {
                iconRect.moveCenter(r.center());
            } else {
                iconRect.moveTopLeft(QPoint(r.left() + 14, r.center().y() - iconSize.height() / 2));
            }
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
            if (!narrow && !title.isEmpty()) {
                const int textLeft = iconRect.right() + 10;
                QColor textClr = (isCS && !isActivated && !sel) ? c.itemInactiveTextColor : c.itemTextColor;
                painter->setPen(textClr);
                QRect textRect(textLeft, r.top(), r.right() - 8 - textLeft, r.height());
                painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);
            }

            if (sel && !narrow) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(c.itemAccentBarColor);
                painter->drawRoundedRect(QRect(r.left() + 2, r.top() + 7, 3, r.height() - 14), 1, 1);
            }
        }
        painter->restore();
    }
};

QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

QToolButton* makeTopIconButton(QWidget* parent, const QIcon& icon, const QString& toolTip) {
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("topIconButton"));
    button->setIcon(icon);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(22, 22));
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
    resize(1350, 835);

    {
        QSettings pinSettings = AppSettings::create();
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
    constexpr int kSidebarRailWidth = 72;
    left->setFixedWidth(kSidebarRailWidth);

    auto* layout = new QVBoxLayout(left);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(9);
    layout->setAlignment(Qt::AlignTop);

    m_userProfileBar = new ProfileBarWidget([this] { onUserProfileBarClicked(); }, left);
    m_userProfileBar->setObjectName(QStringLiteral("userProfileBar"));
    m_userProfileBar->setFixedHeight(48);
    m_userProfileBar->setToolTip(QStringLiteral("查看并编辑个人信息"));
    auto* profileLay = new QHBoxLayout(m_userProfileBar);
    profileLay->setContentsMargins(4, 4, 4, 4);
    profileLay->setSpacing(0);
    m_userProfileAvatar = new QLabel(m_userProfileBar);
    m_userProfileAvatar->setObjectName(QStringLiteral("sidebarAvatar"));
    m_userProfileAvatar->setFixedSize(38, 38);
    m_userProfileAvatar->setAlignment(Qt::AlignCenter);
    m_userProfileAvatar->setScaledContents(false);
    m_userProfileNick = new QLabel(m_userProfileBar);
    m_userProfileNick->setObjectName(QStringLiteral("userProfileNick"));
    m_userProfileNick->setWordWrap(false);
    m_userProfileNick->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_userProfileNick->hide();
    profileLay->addStretch(1);
    profileLay->addWidget(m_userProfileAvatar, 0, Qt::AlignCenter);
    profileLay->addStretch(1);
    layout->addWidget(m_userProfileBar);

    m_platformTreeModel = new QStandardItemModel(this);

    // -- 在线平台 --
    const QIcon iconOnlineGroup = resourceIcon(QStringLiteral(":/online_platform_icon.svg"),
                                               qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    m_onlineGroup = new QStandardItem(iconOnlineGroup, QString());
    m_onlineGroup->setToolTip(QStringLiteral("在线平台\n若有子项，可点击展开/折叠"));
    m_onlineGroup->setData(QStringLiteral("online"), PlatformIdRole);
    m_onlineGroup->setData(true, IsGroupRole);
    m_onlineGroup->setFlags(m_onlineGroup->flags() & ~Qt::ItemIsDropEnabled);

    // -- 管理后台（可折叠分组） --
    const QIcon iconManageGroup = resourceIcon(QStringLiteral(":/backend_manage_icon.svg"),
                                               qApp->style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_manageGroup = new QStandardItem(iconManageGroup, QString());
    m_manageGroup->setToolTip(QStringLiteral("管理后台\n可点击展开/折叠"));
    m_manageGroup->setData(QStringLiteral("manage"), PlatformIdRole);
    m_manageGroup->setData(true, IsGroupRole);
    m_manageGroup->setFlags(m_manageGroup->flags() & ~Qt::ItemIsDropEnabled);

    const QIcon iconRobot = resourceIcon(QStringLiteral(":/platform_management_icon.svg"),
                                         qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    auto* itemRobot = new QStandardItem(iconRobot, QString());
    itemRobot->setToolTip(QStringLiteral("AI助手"));
    itemRobot->setData(QStringLiteral("robot"), PlatformIdRole);
    itemRobot->setData(false, IsGroupRole);
    itemRobot->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemRobot);

    const QIcon iconReception = resourceIcon(QStringLiteral(":/aggregate_reception_icons/message_icon.svg"),
                                             qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation));
    auto* itemReception = new QStandardItem(iconReception, QString());
    itemReception->setToolTip(QStringLiteral("聚合接待"));
    itemReception->setData(QStringLiteral("aggregate"), PlatformIdRole);
    itemReception->setData(false, IsGroupRole);
    itemReception->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemReception);

    const QIcon iconAiBackend = resourceIcon(QStringLiteral(":/ai_customer_service_backend.svg"),
                                             qApp->style()->standardIcon(QStyle::SP_DesktopIcon));
    auto* itemAiBackend = new QStandardItem(iconAiBackend, QString());
    itemAiBackend->setToolTip(QStringLiteral("AI客服后台"));
    itemAiBackend->setData(QStringLiteral("aiServiceBackend"), PlatformIdRole);
    itemAiBackend->setData(false, IsGroupRole);
    itemAiBackend->setData(false, IsCustomerServiceItemRole);
    m_manageGroup->appendRow(itemAiBackend);

    // -- 客服平台 --
    const QIcon iconCsGroup = resourceIcon(QStringLiteral(":/customer_service_platform_icon.svg"),
                                           qApp->style()->standardIcon(QStyle::SP_DirIcon));
    m_csGroup = new QStandardItem(iconCsGroup, QString());
    m_csGroup->setToolTip(QStringLiteral("客服平台\n可点击展开/折叠"));
    m_csGroup->setData(QStringLiteral("cs"), PlatformIdRole);
    m_csGroup->setData(true, IsGroupRole);
    m_csGroup->setFlags(m_csGroup->flags() & ~Qt::ItemIsDropEnabled);

    struct CsItem { const char* name; const char* id; };
    CsItem csItems[] = {{"千牛", "qianniu"}, {"拼多多", "pinduoduo"}, {"抖店", "douyin"}};
    for (const auto& cs : csItems) {
        const QString platformId = QString::fromUtf8(cs.id);
        const QString csName = QString::fromUtf8(cs.name);
        QIcon itemIcon = customerServiceIcon(platformId);
        if (itemIcon.isNull())
            itemIcon = qApp->style()->standardIcon(QStyle::SP_DialogApplyButton);
        auto* item = new QStandardItem(itemIcon, QString());
        item->setToolTip(csName);
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
    m_platformTree->setItemDelegate(new PlatformTreeDelegate(m_platformTree));
    m_platformTree->setHeaderHidden(true);
    m_platformTree->setIndentation(0);
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
    m_platformTree->expand(m_onlineGroup->index());
    m_platformTree->collapse(m_manageGroup->index());
    m_platformTree->collapse(m_csGroup->index());
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
        m_userProfileBar->setToolTip(QStringLiteral("%1\n%2")
                                         .arg(m_username, QStringLiteral("查看并编辑个人信息")));
        m_userId = 0;
        const int side = (m_userProfileNick && m_userProfileNick->isHidden()) ? 32 : 40;
        QPixmap pm;
        const qreal dpr = devicePixelRatioF();
        QPixmap canvas(QSize(side, side) * dpr);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::transparent);
        QSvgRenderer renderer(QStringLiteral(":/default_avatar_icon.svg"));
        QPainter painter(&canvas);
        renderer.render(&painter, QRectF(0, 0, canvas.width(), canvas.height()));
        pm = canvas;
        pm = roundedSidebarAvatarPixmap(pm, side, dpr, 8);
        m_userProfileAvatar->setPixmap(pm);
        return;
    }
    m_userId = u->id;
    const QString shown = u->displayName.isEmpty() ? u->username : u->displayName;
    m_userProfileNick->setText(shown);

    m_userProfileBar->setToolTip(QStringLiteral("%1\n%2")
                                     .arg(shown, QStringLiteral("查看并编辑个人信息")));

    const int side = (m_userProfileNick && m_userProfileNick->isHidden()) ? 32 : 40;
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
        if (m_robotAssistantWidget)
            m_robotAssistantWidget->refreshLocalUserProfile();
    }
}

// ==================== Top Bar ====================

QWidget* MainWindow::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("topBar");
    bar->setFixedHeight(56);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(20, 0, 18, 0);
    layout->setSpacing(12);

    auto* helpBtn = makeTopIconButton(bar, QIcon(QStringLiteral(":/question_mark_icon.svg")),
                                      QStringLiteral("帮助与更多"));

    auto* titleWrap = new QWidget(bar);
    titleWrap->setObjectName(QStringLiteral("topTitleWrap"));
    auto* titleLayout = new QVBoxLayout(titleWrap);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    titleLayout->setAlignment(Qt::AlignVCenter);
    auto* topTitle = new QLabel(QStringLiteral("AI 客服工作台"), titleWrap);
    topTitle->setObjectName(QStringLiteral("topTitle"));
    titleLayout->addWidget(topTitle);

    auto* readyWrap = new QWidget(bar);
    readyWrap->setObjectName("readyWrap");
    auto* readyLayout = new QHBoxLayout(readyWrap);
    readyLayout->setContentsMargins(10, 5, 10, 5);
    readyLayout->setSpacing(6);
    auto* readyIcon = new QLabel(readyWrap);
    readyIcon->setPixmap(resourcePixmap(QStringLiteral(":/system_ready_icon.svg"), QSize(18, 18),
                                        qApp->style()->standardIcon(QStyle::SP_ArrowUp)));
    auto* readyText = new QLabel(QStringLiteral("系统就绪"), readyWrap);
    readyText->setObjectName("readyText");
    readyLayout->addWidget(readyIcon);
    readyLayout->addWidget(readyText);
    layout->addWidget(titleWrap);
    layout->addWidget(readyWrap);
    layout->addStretch(1);
    m_btnPinTop = makeTopIconButton(bar, resourceIcon(QStringLiteral(":/before_pinning.svg")),
                                    QStringLiteral("置顶"));
    layout->addWidget(m_btnPinTop);
    layout->addWidget(helpBtn);

    connect(m_btnPinTop, &QToolButton::clicked, this, [this]() {
        applyAlwaysOnTop(!m_alwaysOnTop);
    });
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::openAppHelpDialog);
    helpBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(helpBtn, &QToolButton::customContextMenuRequested, this, [this, helpBtn](const QPoint& pos) {
        QMenu menu(helpBtn);
        QAction* bugLog = menu.addAction(QStringLiteral("查看 Bug 修复日志"));
        QAction* pinTop = menu.addAction(m_alwaysOnTop ? QStringLiteral("取消窗口置顶")
                                                       : QStringLiteral("窗口置顶"));
        QAction* triggered = menu.exec(helpBtn->mapToGlobal(pos));
        if (triggered == bugLog) {
            openBugLogDialog();
        } else if (triggered == pinTop) {
            applyAlwaysOnTop(!m_alwaysOnTop);
        }
    });
    updatePinTopButtonUi();
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
        totalHeight += isGroup ? 48 : 52;
        if (m_platformTree->isExpanded(index)) {
            int childCount = m_platformTreeModel->rowCount(index);
            totalHeight += childCount * 52;
        }
    }
    m_platformTree->setMinimumHeight(totalHeight + 12);
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
        showSystemReadyPage();
        openAggregateChatForm();
        return;
    }
    if (id == QLatin1String("aiServiceBackend")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showSystemReadyPage();
        openAiCustomerServiceBackendWindow();
        return;
    }
    if (id == QLatin1String("robot")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        openRobotAssistantPage();
        return;
    }
    if (id == QLatin1String("manage")) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        showPlaceholderPage(QStringLiteral("管理后台"));
        return;
    }

    // Customer service item — not activated yet
    bool isCS = idx.data(IsCustomerServiceItemRole).toBool();
    bool isActivated = idx.data(IsActivatedRole).toBool();
    if (isCS && !isActivated) {
        hideCurrentFloatWindow();
        m_activeWindowId.clear();
        const QString name = platformTreeRowLabel(idx);
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
    showPlaceholderPage(platformTreeRowLabel(idx));
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
    applyMainWindowTheme(ApplyStyle::MainWindowTheme::Default);
}

void MainWindow::applyMainWindowTheme(ApplyStyle::MainWindowTheme theme)
{
    Q_UNUSED(theme)
    m_mainWindowTheme = ApplyStyle::MainWindowTheme::Default;
    setStyleSheet(ApplyStyle::mainWindowStyle());
    if (m_platformTree && m_platformTree->viewport())
        m_platformTree->viewport()->update();
    if (m_aggregateChatForm)
        m_aggregateChatForm->applyTheme(m_mainWindowTheme);
    if (m_robotAssistantWidget)
        m_robotAssistantWidget->applyTheme(m_mainWindowTheme);
}

static constexpr int kOneClickMinOnline = 2;
static constexpr int kOneClickMaxOnline = 50;

QWidget* MainWindow::buildReadyPage()
{
    auto* center = new QWidget(this);
    center->setObjectName("centerArea");

    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(48, 42, 48, 32);
    layout->setSpacing(18);
    layout->addStretch(1);

    m_readyCard = makeCard(center);
    m_readyCard->setObjectName("readyCard");
    m_readyCard->setFixedWidth(640);

    auto* cardLayout = new QVBoxLayout(m_readyCard);
    cardLayout->setContentsMargins(36, 30, 36, 30);
    cardLayout->setSpacing(10);

    auto* rocketRow = new QWidget(m_readyCard);
    auto* rocketRowLayout = new QHBoxLayout(rocketRow);
    rocketRowLayout->setContentsMargins(0, 0, 0, 0);
    rocketRowLayout->setSpacing(0);
    rocketRowLayout->addStretch(1);

    auto* rocketWrap = new QFrame(rocketRow);
    rocketWrap->setObjectName("rocketWrap");
    rocketWrap->setFixedSize(58, 58);
    rocketWrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* rocketLayout = new QGridLayout(rocketWrap);
    rocketLayout->setContentsMargins(0, 0, 0, 0);
    rocketLayout->setSpacing(0);
    auto* rocket = new QLabel(rocketWrap);
    constexpr int kRocketLogical = 30;
    const QPixmap rocketPm = resourcePixmap(QStringLiteral(":/rocket_icon.svg"), QSize(kRocketLogical, kRocketLogical),
                                             qApp->style()->standardIcon(QStyle::SP_ArrowUp));
    rocket->setPixmap(rocketPm);
    rocket->setAlignment(Qt::AlignCenter);
    rocket->setScaledContents(false);
    if (!rocketPm.isNull()) {
        const qreal dpr = rocketPm.devicePixelRatioF() > 0 ? rocketPm.devicePixelRatioF() : 1.0;
        rocket->setFixedSize(qMax(1, qRound(rocketPm.width() / dpr)), qMax(1, qRound(rocketPm.height() / dpr)));
    } else {
        rocket->setFixedSize(kRocketLogical, kRocketLogical);
    }
    rocketLayout->addWidget(rocket, 0, 0, Qt::AlignCenter);

    rocketRowLayout->addWidget(rocketWrap);
    rocketRowLayout->addStretch(1);

    m_readyTitle = new QLabel(QStringLiteral("开始今天的接待工作"), m_readyCard);
    m_readyTitle->setObjectName("readyTitle");
    m_readyTitle->setAlignment(Qt::AlignHCenter);

    auto* divider = new QFrame(m_readyCard);
    divider->setObjectName("divider");
    divider->setFixedHeight(1);
    divider->setFixedWidth(160);

    m_readySubtitle = new QLabel(QStringLiteral("从左侧选择平台，或直接使用下方快捷入口"), m_readyCard);
    m_readySubtitle->setObjectName("readySubtitle");
    m_readySubtitle->setAlignment(Qt::AlignHCenter);

    cardLayout->addWidget(rocketRow);
    cardLayout->addWidget(m_readyTitle);
    cardLayout->addWidget(divider, 0, Qt::AlignHCenter);
    cardLayout->addWidget(m_readySubtitle);

    auto* quickRow = new QWidget(center);
    auto* quickLayout = new QHBoxLayout(quickRow);
    quickLayout->setContentsMargins(0, 2, 0, 0);
    quickLayout->setSpacing(16);
    quickLayout->setAlignment(Qt::AlignHCenter);

    auto makeQuick = [&](const QIcon& icon, const QString& text) -> QToolButton* {
        auto* btn = new QToolButton(quickRow);
        btn->setObjectName("quickCard");
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(icon);
        btn->setIconSize(QSize(32, 32));
        btn->setText(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setAutoRaise(false);
        btn->setFixedSize(156, 112);
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
            int v = cur;
            if (showStyledIntInputDialog(
                    this,
                    mainWindowTheme(),
                    QStringLiteral("在线窗口上限"),
                    QStringLiteral("上限为 %1～%2，建议不超过 30 以保持流畅：")
                        .arg(kOneClickMinOnline)
                        .arg(kOneClickMaxOnline),
                    cur,
                    kOneClickMinOnline,
                    kOneClickMaxOnline,
                    1,
                    &v)) {
                setOneClickMaxOnlineLimit(v);
            }
        }
    });
    connect(btnPick, &QToolButton::clicked, this, &MainWindow::startOneClickAggregate);
    auto* btnEmbed = makeQuick(resourceIcon(QStringLiteral(":/start_or_stop_rpa_icon.svg"),
                                            qApp->style()->standardIcon(QStyle::SP_FileDialogListView)),
                               QStringLiteral("Python 服务端"));
    m_btnPythonServiceConnection = btnEmbed;
    m_btnPythonServiceConnection->setToolTip(QStringLiteral("配置并测试独立 Python 服务端连接"));
    auto* btnStart = makeQuick(resourceIcon(QStringLiteral(":/quick_launch_application_icon.svg"),
                                            qApp->style()->standardIcon(QStyle::SP_DialogOkButton)),
                               QStringLiteral("快速启动应用"));
    m_btnQuickStart = btnStart;
    m_btnQuickStart->setToolTip(
        QStringLiteral("左键：按列表快速启动（受「数量上限」约束，默认前 10 项）\n右键：管理应用列表 / 设置数量上限"));
    connect(btnEmbed, &QToolButton::clicked, this, &MainWindow::openPythonServiceConnectionDialog);
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
            QSettings settings = AppSettings::create();
            const int cur = qBound(1, settings.value(QStringLiteral("quickLaunch/maxLaunchCount"), 10).toInt(), 30);
            int v = cur;
            if (showStyledIntInputDialog(
                    this,
                    mainWindowTheme(),
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
                    &v)) {
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

void MainWindow::openPythonServiceConnectionDialog()
{
    PythonServiceConnectionDialog dlg(this, this);
    dlg.exec();
}

int MainWindow::oneClickMaxOnlineLimit() const
{
    QSettings s = AppSettings::create();
    int v = s.value(QStringLiteral("oneClickAggregate/maxOnline"), 10).toInt();
    return qBound(kOneClickMinOnline, v, kOneClickMaxOnline);
}

void MainWindow::setOneClickMaxOnlineLimit(int n)
{
    int v = qBound(kOneClickMinOnline, n, kOneClickMaxOnline);
    QSettings s = AppSettings::create();
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
        QSettings pinSettings = AppSettings::create();
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
    QSettings settings = AppSettings::create();
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
    QSettings settings = AppSettings::create();
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

    QSettings settings = AppSettings::create();
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
        m_platformTree->expand(m_csGroup->index());
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
        item->setToolTip(info.platformName);
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

            QMenu menu(this);
            QAction* actAddWindow = menu.addAction(QStringLiteral("添加新窗口"));
            QAction* actRemoveAll = nullptr;
            if (!onlinePlatformIds.isEmpty()) {
                menu.addSeparator();
                actRemoveAll = menu.addAction(QStringLiteral("删除全部平台"));
            }
            QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
            if (chosen == actAddWindow) {
                openAddWindowDialog();
            } else if (actRemoveAll && chosen == actRemoveAll) {
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

    QAction* chosen = menu.exec(m_platformTree->viewport()->mapToGlobal(pos));
    if (chosen == actPrimary) {
        removeOnlinePlatformItem(id);
    }
}

// ==================== Aggregate Chat ====================

void MainWindow::openAggregateChatForm()
{
    if (!m_aggregateReceptionWindow) {
        auto* w = new QMainWindow(nullptr);
        w->setAttribute(Qt::WA_DeleteOnClose, true);
        w->setWindowTitle(QStringLiteral("聚合接待"));
        w->setMinimumSize(620, 650);
        w->resize(1280, 820);
        m_aggregateReceptionWindow = w;
        m_aggregateChatForm = new AggregateChatForm(m_username, w);
        w->setCentralWidget(m_aggregateChatForm);
        m_aggregateChatForm->applyTheme(m_mainWindowTheme);
        connect(m_aggregateReceptionWindow, &QObject::destroyed, this, [this] {
            m_aggregateReceptionWindow = nullptr;
            m_aggregateChatForm = nullptr;
        });
    }
    m_aggregateReceptionWindow->show();
    m_aggregateReceptionWindow->raise();
    m_aggregateReceptionWindow->activateWindow();
    // 若保留「聚合接待」为当前项，关窗时焦点回主窗会再次触发 currentChanged 从而重复 open——打开后即清选中。
    if (m_platformTree) {
        const QModelIndex cur = m_platformTree->currentIndex();
        if (cur.isValid() && cur.data(PlatformIdRole).toString() == QLatin1String("aggregate"))
            m_platformTree->clearSelection();
    }
}

void MainWindow::openRobotAssistantPage()
{
    if (!m_robotAssistantWidget) {
        m_robotAssistantWidget = new RobotAssistantWidget(m_username, this);
        m_robotAssistantWidget->applyTheme(m_mainWindowTheme);
        m_centerStack->addWidget(m_robotAssistantWidget);
    }
    m_centerStack->setCurrentWidget(m_robotAssistantWidget);
}

void MainWindow::openAiCustomerServiceBackendWindow(bool goToApiModelPage)
{
    if (!m_aiCustomerServiceBackendWindow) {
        m_aiCustomerServiceBackendWindow = new AiCustomerServiceBackendWindow(nullptr);
        connect(m_aiCustomerServiceBackendWindow, &QObject::destroyed, this, [this] {
            m_aiCustomerServiceBackendWindow = nullptr;
        });
        connect(
            m_aiCustomerServiceBackendWindow, &AiCustomerServiceBackendWindow::aiProviderConfigChanged, this,
            [this] {
                if (m_robotAssistantWidget)
                    m_robotAssistantWidget->onExternalProviderConfigChanged();
            });
    }
    m_aiCustomerServiceBackendWindow->show();
    m_aiCustomerServiceBackendWindow->raise();
    m_aiCustomerServiceBackendWindow->activateWindow();
    if (goToApiModelPage)
        m_aiCustomerServiceBackendWindow->focusApiModelPage();
    // 与「聚合接待」独立窗一致：打开后清树选中，避免关窗时焦点回主窗再次触发 currentChanged 重复打开。
    if (m_platformTree) {
        const QModelIndex cur = m_platformTree->currentIndex();
        if (cur.isValid() && cur.data(PlatformIdRole).toString() == QLatin1String("aiServiceBackend"))
            m_platformTree->clearSelection();
    }
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
    if (m_aiCustomerServiceBackendWindow) {
        m_aiCustomerServiceBackendWindow->close();
    }
    if (m_aggregateReceptionWindow) {
        m_aggregateReceptionWindow->close();
    }
    detachAllWindows();
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
    auto* statusWrap = new QWidget(this);
    statusWrap->setObjectName("statusBarWrap");
    auto* statusLayout = new QHBoxLayout(statusWrap);
    statusLayout->setContentsMargins(2, 0, 2, 0);
    statusLayout->setSpacing(6);

    m_statusMessage = new QLabel(statusWrap);
    m_statusSeparator = new QLabel(statusWrap);
    m_statusTime = new QLabel(statusWrap);
    m_statusMessage->setObjectName("statusMessage");
    m_statusSeparator->setObjectName("statusSeparator");
    m_statusTime->setObjectName("statusTime");
    m_statusMessage->setCursor(Qt::PointingHandCursor);
    m_statusMessage->setToolTip(QStringLiteral("左键换一句，右键管理文案"));
    m_statusSeparator->setText(QStringLiteral("·"));
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
