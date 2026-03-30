#include "helpcenterdialog.h"

#include "../utils/applystyle.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVector>

namespace {

/** 帮助目录：分组行右侧箭头；子项整行自绘（indentation=0）避免左侧「分支带」与选中块色差 */
class HelpTocDelegate : public QStyledItemDelegate
{
    struct TocPalette {
        QColor selBg;
        QColor selText;
        QColor hovBg;
        QColor hovText;
        QColor normalText;
    };

    static TocPalette tocPalette(ApplyStyle::MainWindowTheme theme)
    {
        switch (theme) {
        case ApplyStyle::MainWindowTheme::Cool:
            return {QColor(QStringLiteral("#0284c7")), QColor(QStringLiteral("#f8fafc")),
                    QColor(QStringLiteral("#1e293b")), QColor(QStringLiteral("#f1f5f9")),
                    QColor(QStringLiteral("#e2e8f0"))};
        case ApplyStyle::MainWindowTheme::Warm:
            return {QColor(QStringLiteral("#b45309")), QColor(QStringLiteral("#fffbeb")),
                    QColor(QStringLiteral("#3d3835")), QColor(QStringLiteral("#fff7ed")),
                    QColor(QStringLiteral("#f5efe6"))};
        case ApplyStyle::MainWindowTheme::Default:
        default:
            return {QColor(QStringLiteral("#3F3F46")), QColor(QStringLiteral("#FAFAFA")),
                    QColor(QStringLiteral("#E4E4E7")), QColor(QStringLiteral("#18181B")),
                    QColor(QStringLiteral("#27272A"))};
        }
    }

public:
    explicit HelpTocDelegate(QTreeWidget* tree, ApplyStyle::MainWindowTheme theme, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_tree(tree)
        , m_theme(theme)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QTreeWidgetItem* item = m_tree ? m_tree->itemFromIndex(index) : nullptr;
        if (!item || item->childCount() == 0) {
            paintLeaf(painter, option, index, item);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const TocPalette c = tocPalette(m_theme);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRect cell = opt.rect.adjusted(4, 2, -4, -2);
        if (opt.state.testFlag(QStyle::State_MouseOver)) {
            painter->setBrush(c.hovBg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(cell, 8, 8);
        }

        QFont f = opt.font;
        f.setBold(true);
        painter->setFont(f);
        painter->setPen(c.normalText);

        static const QIcon expandIcon(QStringLiteral(":/fold_arrow_to_expand_icon.svg"));
        static const QIcon collapseIcon(QStringLiteral(":/fold_arrow_to_collapse_icon.svg"));
        const QPixmap pix = (item->isExpanded() ? collapseIcon : expandIcon).pixmap(QSize(16, 16));

        const int rightReserve = pix.isNull() ? 8 : 8 + pix.width() + 4;
        const QRect textRect(cell.left() + 8, cell.top(), cell.width() - 8 - rightReserve, cell.height());
        const QString elided = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elided);

        if (!pix.isNull()) {
            const QPoint pt(cell.right() - pix.width() - 4, cell.center().y() - pix.height() / 2);
            painter->drawPixmap(pt, pix);
        }
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override
    {
        Q_UNUSED(model);
        if (!m_tree || event->type() != QEvent::MouseButtonRelease)
            return false;
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() != Qt::LeftButton)
            return false;

        QTreeWidgetItem* item = m_tree->itemFromIndex(index);
        if (!item || item->childCount() == 0)
            return false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint p = me->position().toPoint();
#else
        const QPoint p = me->pos();
#endif
        if (!option.rect.contains(p))
            return false;

        item->setExpanded(!item->isExpanded());
        return true;
    }

private:
    void paintLeaf(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
                   QTreeWidgetItem* item) const
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const TocPalette c = tocPalette(m_theme);
        const QRect r = opt.rect.adjusted(4, 2, -4, -2);
        const bool isChild = item && item->parent();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (opt.state.testFlag(QStyle::State_Selected)) {
            painter->setBrush(c.selBg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(r, 8, 8);
            painter->setPen(c.selText);
        } else if (opt.state.testFlag(QStyle::State_MouseOver)) {
            painter->setBrush(c.hovBg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(r, 8, 8);
            painter->setPen(c.hovText);
        } else {
            painter->setPen(c.normalText);
        }

        QFont f = opt.font;
        f.setBold(false);
        painter->setFont(f);
        const int textLeft = r.left() + 10 + (isChild ? 12 : 0);
        const QRect textRect(textLeft, r.top(), r.right() - 8 - textLeft, r.height());
        const QString elided = opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elided);
        painter->restore();
    }

    QTreeWidget* m_tree = nullptr;
    ApplyStyle::MainWindowTheme m_theme = ApplyStyle::MainWindowTheme::Default;
};

struct HelpContentSection {
    QString anchor;
    QString tocLabel;
    QString html;
};

} // namespace

HelpCenterDialog::HelpCenterDialog(InitialSection initialSection, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("帮助中心"));
    setMinimumSize(600, 400);
    resize(780, 540);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(14);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter);

    m_toc = new QTreeWidget;
    m_toc->setObjectName(QStringLiteral("helpToc"));
    m_toc->setHeaderHidden(true);
    m_toc->setRootIsDecorated(false);
    m_toc->setAnimated(true);
    m_toc->setIndentation(0);
    m_toc->setExpandsOnDoubleClick(false);
    m_toc->setUniformRowHeights(false);
    m_toc->setItemDelegate(new HelpTocDelegate(
        m_toc, ApplyStyle::loadSavedMainWindowTheme(), m_toc));
    m_toc->setMinimumWidth(196);
    m_toc->setMaximumWidth(280);
    m_toc->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_toc->setAttribute(Qt::WA_StyledBackground, true);
    if (QWidget* vp = m_toc->viewport())
        vp->setAttribute(Qt::WA_StyledBackground, true);
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        m_toc->setStyle(fusion);
    splitter->addWidget(m_toc);

    m_browser = new QTextBrowser;
    m_browser->setObjectName(QStringLiteral("helpBrowser"));
    m_browser->setOpenExternalLinks(false);
    m_browser->setOpenLinks(false);
    splitter->addWidget(m_browser);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({210, 570});

    populateContent();

    connect(m_toc, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (!current)
                    return;
                const QString anchor = current->data(0, Qt::UserRole).toString();
                if (!anchor.isEmpty())
                    m_browser->scrollToAnchor(anchor);
            });

    setStyleSheet(
        ApplyStyle::helpDialogStyle(ApplyStyle::loadSavedMainWindowTheme()));

    QTreeWidgetItem* startItem = nullptr;
    QTreeWidgetItem* firstLeaf = nullptr;
    for (QTreeWidgetItemIterator it(m_toc); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const QString anchor = item->data(0, Qt::UserRole).toString();
        if (anchor.isEmpty())
            continue;
        if (!firstLeaf)
            firstLeaf = item;
        if (initialSection == InitialSection::BugLog && anchor.startsWith(QLatin1String("bug"))) {
            startItem = item;
            break;
        }
    }
    if (!startItem)
        startItem = firstLeaf;
    if (startItem) {
        m_toc->setCurrentItem(startItem);
        m_toc->scrollToItem(startItem, QAbstractItemView::PositionAtCenter);
    }
}

void HelpCenterDialog::openUsageGuide(QWidget* parent)
{
    HelpCenterDialog dlg(InitialSection::UsageGuide, parent);
    dlg.exec();
}

void HelpCenterDialog::openBugLog(QWidget* parent)
{
    HelpCenterDialog dlg(InitialSection::BugLog, parent);
    dlg.exec();
}

void HelpCenterDialog::populateContent()
{
    QVector<HelpContentSection> sections;
    const ApplyStyle::MainWindowTheme theme = ApplyStyle::loadSavedMainWindowTheme();

    // ---- 使用说明 ----
    sections.append({
        QStringLiteral("help_overview"), QStringLiteral("\u2022 基本操作"),
        QStringLiteral(
            "<h3>基本操作</h3>"
            "<p>1. 左侧「在线平台」「客服平台」列表用于选择和管理各个平台窗口。</p>"
            "<p>2. 点击顶部「添加新窗口」可从当前已打开的窗口中选择并关联到平台项。</p>"
            "<p>3. 支持多选窗口批量添加，添加过程中会显示进度提示。</p>")
    });
    sections.append({
        QStringLiteral("help_oneclick"), QStringLiteral("\u2022 一键聚合"),
        QStringLiteral(
            "<h3>一键聚合</h3>"
            "<p>在主界面<b>系统就绪</b>页中部，点击「一键聚合」可自动扫描当前桌面的顶层窗口，把<b>尚未被本软件托管</b>的窗口加入托管队列，"
            "效果与连续使用「添加新窗口」批量添加类似（含进度提示）。</p>"
            "<p><b>识别为客服平台的窗口</b>（如标题或进程名符合千牛、拼多多、抖店等规则）会<b>优先加入队列</b>，且<b>不计入「在线窗口上限」</b>，"
            "避免店铺端窗口被误截断。</p>"
            "<p><b>其余窗口</b>（含微信、浏览器类及普通应用）在加入时会占用「在线窗口」计数；当计数达到上限后，"
            "本轮扫描中<b>不再加入更多非客服窗口</b>，以减轻卡顿与资源占用。默认上限为 10，可在 <b>2～50</b> 范围内调整。</p>"
            "<p><b>右键</b>「一键聚合」可打开「设置在线窗口上限…」；按钮悬停提示中会显示当前上限。</p>"
            "<p>若当前没有可加入的新窗口，状态栏会提示「未发现可聚合窗口」。</p>")
    });
    sections.append({
        QStringLiteral("help_aggregate"), QStringLiteral("\u2022 聚合接待"),
        QStringLiteral(
            "<h3>聚合接待</h3>"
            "<p>左侧列表中的「聚合接待」用于在<b>本软件内</b>统一查看各平台会话并手动回复客户（与就绪页的「一键聚合」不同："
            "后者是把外部窗口批量纳入托管）。</p>"
            "<p>点击「聚合接待」即可进入，浮窗类托管窗口会自动隐藏，切回具体平台项后自动恢复。</p>")
    });
    sections.append({
        QStringLiteral("help_rpa"), QStringLiteral("\u2022 管理启动/停止 RPA"),
        QStringLiteral(
            "<h3>管理启动/停止 RPA</h3>"
            "<p>在系统就绪页点击「管理启动/停止RPA」，可勾选平台后统一<b>启动</b>或<b>停止</b>后台 RPA 进程。当前支持：<b>微信 PC</b>、<b>千牛 PC</b>、<b>拼多多网页</b>。</p>"
            "<p><b>启动</b>：仅在「已勾选且当前未运行」的平台上拉起进程；若勾选项均已运行，会弹出说明提示。</p>"
            "<p><b>停止</b>：结束「已勾选且正在运行」的平台进程；再次打开本窗口时，正在运行的项会<b>默认勾选</b>，便于直接停止。</p>"
            "<p>可使用「全选」「取消全选」快速调整勾选。</p>"
            "<p><b>运行环境</b>：程序会在项目下的 <code>python</code> 目录作为工作目录执行 "
            "<code>python -m rpa.main --platform &lt;wechat | qianniu | pdd&gt;</code>。"
            "请确保本机已安装 Python，且命令行中 <code>python</code> 可用；若启动失败，状态栏会提示检查 PATH。</p>"
            "<p><b>右键</b>「管理启动/停止RPA」可打开<b>控制台输出</b>窗口，查看各平台子进程的标准输出日志（便于排查 OCR、数据库写入等问题）。</p>"
            "<p>各平台消息读写还依赖窗口区域校准与配置文件等，请按软件内的校准向导或项目文档完成设置。</p>")
    });
    sections.append({
        QStringLiteral("help_disconnect"), QStringLiteral("\u2022 断开窗口关联"),
        QStringLiteral(
            "<h3>断开窗口关联</h3>"
            "<p>在左侧「在线平台」或「客服平台」列表中，<b>右键</b>点击对应平台项，选择「断开关联」或「删除」即可断开与主窗口的关联（不会关闭该应用窗口本身）。</p>"
            "<p style='color:%1;'>&#9888; 请勿使用嵌入窗口标题栏上的「最小化」「最大化」「关闭」按钮，可能引发异常或白屏。需断开关联时请通过上述右键菜单操作。</p>")
            .arg(ApplyStyle::helpDialogHtmlWarningColor(theme))
    });
    sections.append({
        QStringLiteral("help_quicklaunch"), QStringLiteral("\u2022 快速启动应用"),
        QStringLiteral(
            "<h3>快速启动应用</h3>"
            "<p>主界面就绪页中部的「快速启动应用」卡片，用于按列表批量启动常用外部程序。</p>"
            "<p><b>左键</b>：按列表配置的顺序依次启动；受「数量上限」约束（默认只处理前 <b>10</b> 项，可在卡片上<b>右键 → 设置数量上限</b>修改，范围 1～30）。"
            "完成后会弹出本次结果汇总（已启动 / 已跳过 / 失败 / 因上限未尝试等）。</p>"
            "<p><b>右键</b>：「管理应用列表」「设置数量上限」等。</p>"
            "<h4 style='margin-top:1em;margin-bottom:0.35em;font-size:14px;'>管理应用列表 — 详细说明</h4>"
            "<ol style='margin-top:0.4em;padding-left:1.35em;line-height:1.55;'>"
            "<li>支持添加 <code>.exe</code> 或 Windows <code>.lnk</code> 快捷方式；快捷方式将按系统方式打开。</li>"
            "<li>使用分组整理应用；「默认」分组在配置中对应<b>空分组名</b>。可拖拽分组或应用调整顺序。</li>"
            "<li>应用行左侧可勾选；「删除勾选」批量删除；「删除全部」清空列表（仍会保留空的「默认」分组）。分组默认展开，可点击折叠。</li>"
            "<li>「黑白名单…」可设置扫描强度（<b>严格 / 标准 / 宽松</b>），并查看未收录与可收录的快捷方式；勾选后可在黑白名单间移动或取消强制规则。</li>"
            "<li>「自动扫描」按上述强度与黑白名单从开始菜单、桌面等目录收集 <code>.lnk</code>；部分目录与系统可执行文件仍会硬过滤。</li>"
            "<li>双击「名称」列可编辑显示名；「目标路径」列为只读，请用「更改目标…」修改路径。</li>"
            "<li>「只启动未运行的应用」：通过 <code>tasklist</code> 按进程名检测，并复用「添加新窗口」同款顶层窗口枚举——若已有匹配进程名的可见顶层窗口也会判定为已运行并跳过。"
            "更细的判定与局限见该复选框的<b>悬停提示</b>。</li>"
            "<li>主界面「快速启动应用」<b>右键 → 设置数量上限</b>：左键启动时只处理列表前 <b>N</b> 项；<b>路径为空的行也占用序号</b>。"
            "请勿一次启动过多应用，以免系统卡顿、内存或 CPU 占用过高、部分软件并发异常等，<b>后果自负</b>。</li>"
            "<li>左键快速启动结束后会弹出本次启动结果（含因数量上限未尝试的条目说明）。</li>"
            "<li>微信等应用仍按各自默认方式启动。</li>"
            "</ol>")
    });
    sections.append({
        QStringLiteral("help_troubleshoot"), QStringLiteral("\u2022 故障排查"),
        QStringLiteral(
            "<h3>故障排查</h3>"
            "<p>若遇到窗口嵌入、浮窗跟随或快速启动异常，可关注状态栏提示。</p>"
            "<p>RPA 相关异常可右键「管理启动/停止RPA」打开控制台输出，查看对应平台的运行日志；"
            "亦可确认 Python 是否可用、<code>python/rpa</code> 依赖是否已按说明安装。</p>")
    });

    // ---- Bug 修复日志 ----
    sections.append({
        QStringLiteral("bug_float_style"), QStringLiteral("\u2022 浮窗样式异常"),
        QStringLiteral(
            "<h3>浮窗跟随窗口样式异常</h3>"
            "<p><b>问题：</b>部分外部窗口被修改了原始样式（标题栏、边框丢失或恢复异常）。</p>"
            "<p><b>原因：</b>在 setupFloatFollow / detachFloatFollow 中直接改写了窗口的样式标志。</p>"
            "<p><b>修复：</b>仅调整必要的扩展样式和 Owner，不再强制改写 WS_OVERLAPPEDWINDOW 等样式。</p>")
    });
    sections.append({
        QStringLiteral("bug_disconnect"), QStringLiteral("\u2022 窗口断开关联"),
        QStringLiteral(
            "<h3>外部窗口断开关联</h3>"
            "<p><b>问题：</b>直接点击外部窗口（如微信）自身的关闭按钮时，可能出现白屏、列表未移除等异常。</p>"
            "<p><b>原因：</b>外部窗口关闭行为因应用和系统环境差异大（隐藏到托盘、白屏挂起等），无法统一可靠检测。</p>"
            "<p><b>建议：</b>请通过左侧平台列表「右键 &rarr; 断开关联 / 删除」来安全释放窗口。</p>")
    });
    sections.append({
        QStringLiteral("bug_batch_add"), QStringLiteral("\u2022 多选与进度提示"),
        QStringLiteral(
            "<h3>\"添加新窗口\"多选与进度提示</h3>"
            "<p><b>问题：</b>一次只能添加一个窗口，重复操作繁琐；添加过程缺乏进度反馈。</p>"
            "<p><b>修复：</b>支持列表多选加入队列逐个添加，并增加遮罩 + 进度文本提示。</p>")
    });
    sections.append({
        QStringLiteral("bug_enum_noise"), QStringLiteral("\u2022 枚举噪声过滤"),
        QStringLiteral(
            "<h3>顶层窗口枚举噪声</h3>"
            "<p><b>问题：</b>窗口列表中出现系统输入法等辅助窗口，容易误选。</p>"
            "<p><b>原因：</b>枚举顶层窗口时仅按可见和 Owner 过滤，未按进程/标题进一步排除。</p>"
            "<p><b>修复：</b>对 TextInputHost.exe 进程以及标题包含\"Windows 输入体验\"的窗口进行过滤。</p>")
    });
    sections.append({
        QStringLiteral("bug_aggregate_ui"), QStringLiteral("\u2022 聚合接待界面优化"),
        QStringLiteral(
            "<h3>聚合接待界面视觉与体验</h3>"
            "<p><b>问题：</b>早期版本风格偏暗，缺少渐变和明确分区，消息区域滚动条和气泡样式不统一。</p>"
            "<p><b>修复：</b>采用柔和蓝色渐变背景，统一三栏布局；调整消息气泡配色、隐藏滚动条、统一文字为黑色，并对空态/搜索框做了细致优化。</p>")
    });
    sections.append({
        QStringLiteral("bug_quicklaunch"), QStringLiteral("\u2022 快速启动路径修复"),
        QStringLiteral(
            "<h3>快速启动应用功能</h3>"
            "<p><b>问题：</b>最初使用 cmd/start 启动时，在包含空格或括号的路径下会出现\"找不到\"之类错误提示。</p>"
            "<p><b>原因：</b>命令行参数拼接方式不当，导致 Windows 对带空格路径解析失败。</p>"
            "<p><b>修复：</b>改为对 .exe 使用 QProcess::startDetached；支持 .lnk；可选「只启动未运行的应用」；管理界面支持分组、拖拽排序与编辑显示名；启动后汇总结果。</p>")
    });

    QString html = QStringLiteral(
                       "<html><body style='font-family: \"Microsoft YaHei\", \"Segoe UI\", sans-serif; "
                       "font-size: 13px; color: %1; margin: 0; padding: 16px 16px 16px 0;'>")
                       .arg(ApplyStyle::helpDialogHtmlBodyTextColor(theme));

    QTreeWidgetItem* groupHelp = nullptr;
    QTreeWidgetItem* groupBug = nullptr;

    auto tocLeafLabel = [](const QString& tocLabel) -> QString {
        QString t = tocLabel.trimmed();
        if (t.startsWith(QChar(0x2022)))
            t = t.mid(1).trimmed();
        return t;
    };

    int sectionIndex = 0;
    for (const auto& s : sections) {
        if (s.anchor.startsWith(QLatin1String("help"))) {
            if (!groupHelp) {
                groupHelp = new QTreeWidgetItem(m_toc);
                groupHelp->setText(0, QStringLiteral("软件使用说明"));
                groupHelp->setFlags(Qt::ItemIsEnabled);
                QFont gf = groupHelp->font(0);
                gf.setBold(true);
                groupHelp->setFont(0, gf);
            }
        }
        if (s.anchor.startsWith(QLatin1String("bug"))) {
            if (!groupBug) {
                groupBug = new QTreeWidgetItem(m_toc);
                groupBug->setText(0, QStringLiteral("Bug 修复日志"));
                groupBug->setFlags(Qt::ItemIsEnabled);
                QFont bf = groupBug->font(0);
                bf.setBold(true);
                groupBug->setFont(0, bf);
            }
        }

        QTreeWidgetItem* parent = s.anchor.startsWith(QLatin1String("bug")) ? groupBug : groupHelp;
        auto* leaf = new QTreeWidgetItem(parent);
        leaf->setText(0, tocLeafLabel(s.tocLabel));
        leaf->setData(0, Qt::UserRole, s.anchor);
        leaf->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        const int topMargin = (sectionIndex++ == 0) ? 0 : 28;
        html += QStringLiteral("<a name=\"%1\"></a><div style='margin-top:%2px;'>%3</div>")
                    .arg(s.anchor)
                    .arg(topMargin)
                    .arg(s.html);
    }

    m_toc->expandAll();

    html += QStringLiteral("</body></html>");
    m_browser->setHtml(html);
    m_browser->document()->setDocumentMargin(0);
}
