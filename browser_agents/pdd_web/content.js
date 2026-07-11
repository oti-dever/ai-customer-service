(() => {
  "use strict";

  const WS_URL = "ws://127.0.0.1:8771/pdd_web/page_agent";
  const PLATFORM = "pdd_web";
  const SCAN_INTERVAL_MS = 1500;
  const DEBUG_INTERVAL_MS = 5000;
  const MESSAGE_LIMIT = 30;
  const CONVERSATION_LIMIT = 80;
  const UNREAD_PROCESS_COOLDOWN_MS = 5000;
  const TARGET_HOST = "mms.pinduoduo.com";
  const TARGET_PATH = "/chat-merchant/";
  const CONVERSATION_SELECTORS = [
    ".chat-list-box li",
    "[class*='chat-list'] li",
    ".SessionListGroupItem",
    "[class*='SessionListGroupItem']",
    "[class*='SessionBaseCard']",
    "[id*='leftPanel'] li",
    "[id*='LeftPanel'] li",
    "[class*='leftPanel'] li",
    "[class*='LeftPanel'] li",
    "[class*='session-list'] [class*='item']",
    "[class*='conversation'] [class*='item']"
  ];
  const MESSAGE_SELECTORS = [
    "li[id^='middlePanel_List_']",
    ".msg-list li.onemsg",
    "div[id*='cs-common-message-list-item']",
    "[class*='message-list'] [class*='item']",
    "[class*='MessageList'] [class*='item']",
    "[class*='LayoutDefaultWrapper']"
  ];
  const FALLBACK_MESSAGE_SELECTORS = [
    ".msg-list li"
  ];
  const PANEL_DEBUG_SELECTORS = [
    "[id*='leftPanel']",
    "[id*='middlePanel']",
    "[id*='rightPanel']",
    "[class*='left']",
    "[class*='middle']",
    "[class*='right']",
    "[class*='chat']",
    "[class*='session']",
    ".chat-list-box",
    "[class*='buyer']",
    "[class*='user']"
  ];

  let ws = null;
  let reconnectTimer = null;
  let scanTimer = null;
  let mutationTimer = null;
  let tabId = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
  let lastMessageFingerprint = "";
  let lastConversationFingerprint = "";
  let lastUnreadFingerprint = "";
  let lastShopFingerprint = "";
  let lastKefuName = "";
  let lastDebugAt = 0;
  let observationEnabled = false;
  let unreadProcessing = false;
  let lastHandledUnreadKey = "";
  let lastHandledUnreadAt = 0;

  function isTargetChatPage() {
    return location.hostname === TARGET_HOST && location.pathname.includes(TARGET_PATH);
  }

  function textOf(node) {
    return (node && (node.innerText || node.textContent) || "").trim();
  }

  function firstText(selectors, root = document) {
    for (const selector of selectors) {
      const node = root.querySelector(selector);
      const text = textOf(node);
      if (text) return text;
    }
    return "";
  }

  function firstNonEmptyNodes(selectors, root = document) {
    for (const selector of selectors) {
      const nodes = Array.from(root.querySelectorAll(selector));
      if (nodes.length) return nodes;
    }
    return [];
  }

  function countSelectorHits(selectors) {
    const hits = {};
    for (const selector of selectors) {
      try {
        hits[selector] = document.querySelectorAll(selector).length;
      } catch (_error) {
        hits[selector] = -1;
      }
    }
    return hits;
  }

  function classNameOf(node) {
    const value = node && node.className;
    if (!value) return "";
    return typeof value === "string" ? value : String(value.baseVal || value);
  }

  function sampleSelectorNodes(selectors, limit = 3) {
    const samples = {};
    for (const selector of selectors) {
      let nodes = [];
      try {
        nodes = Array.from(document.querySelectorAll(selector));
      } catch (_error) {
        samples[selector] = { count: -1, nodes: [] };
        continue;
      }
      samples[selector] = {
        count: nodes.length,
        nodes: nodes.slice(0, limit).map((node, index) => {
          const image = node.querySelector("img[src]");
          return {
            index,
            id: node.id || "",
            class_name: classNameOf(node).slice(0, 180),
            text: textOf(node).replace(/\s+/g, " ").slice(0, 180),
            img_src: image ? (image.currentSrc || image.src || "").slice(0, 180) : "",
            role: node.getAttribute("role") || "",
            aria_label: node.getAttribute("aria-label") || ""
          };
        })
      };
    }
    return samples;
  }

  function send(payload) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    ws.send(JSON.stringify({
      platform: PLATFORM,
      tab_id: tabId,
      page_url: location.href,
      occurred_at: new Date().toISOString(),
      ...payload
    }));
    return true;
  }

  function connect() {
    if (!isTargetChatPage()) {
      console.info("[YY PDD agent] ignored non-chat page", location.href);
      return;
    }
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      return;
    }
    try {
      ws = new WebSocket(WS_URL);
    } catch (error) {
      scheduleReconnect();
      return;
    }
    ws.onopen = () => {
      console.info("[YY PDD agent] connected", WS_URL, location.href);
      send({ type: "page_ready", title: document.title });
      scanShopInfo(true);
      startScanning();
    };
    ws.onmessage = (event) => {
      let message = null;
      try {
        message = JSON.parse(event.data);
      } catch (_error) {
        return;
      }
      if (message && message.type === "prepare_reply_draft") {
        prepareReplyDraft(message);
        return;
      }
      if (message && message.type === "configure_observation") {
        configureObservation(message);
      }
    };
    ws.onclose = () => {
      console.info("[YY PDD agent] disconnected");
      scheduleReconnect();
    };
    ws.onerror = (error) => {
      console.warn("[YY PDD agent] websocket error", error);
      scheduleReconnect();
    };
  }

  function scheduleReconnect() {
    if (scanTimer) {
      clearInterval(scanTimer);
      scanTimer = null;
    }
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, 1200);
  }

  function startScanning() {
    if (scanTimer) return;
    scanTimer = setInterval(() => scanObservation(false), SCAN_INTERVAL_MS);
    installMutationObserver();
  }

  function installMutationObserver() {
    if (window.__YY_PDD_AGENT_OBSERVER__) return;
    const observer = new MutationObserver(() => {
      if (mutationTimer) clearTimeout(mutationTimer);
      mutationTimer = setTimeout(() => scanObservation(false), 250);
    });
    observer.observe(document.documentElement, {
      childList: true,
      subtree: true,
      characterData: true
    });
    window.__YY_PDD_AGENT_OBSERVER__ = observer;
  }

  function configureObservation(command) {
    observationEnabled = command.enabled !== false && command.mode !== "idle";
    console.info("[YY PDD agent] observation", observationEnabled ? "enabled" : "disabled", command.mode || "");
    if (!observationEnabled) {
      lastUnreadFingerprint = "";
      unreadProcessing = false;
      return;
    }
    scanObservation(true);
  }

  function scanObservation(force) {
    if (!isTargetChatPage()) return;
    scanShopInfo(force);
    if (!observationEnabled) return;
    scanUnreadConversations(force);
  }

  function scanAll(force) {
    scanObservation(force);
  }

  function scanShopInfo(force) {
    const kefuName = firstText([
      ".nickname",
      ".LoginUserInfo-mainTitle-text",
      "[class*='LoginUserInfo-mainTitle-text']"
    ]);
    const shopName = firstText([
      ".shop-name",
      "[class*='shopName']",
      "[class*='ShopName']"
    ]) || "拼多多";
    const fingerprint = JSON.stringify({ shopName, kefuName, href: location.href });
    if (!force && fingerprint === lastShopFingerprint) return;
    lastShopFingerprint = fingerprint;
    lastKefuName = kefuName || lastKefuName;
    send({
      type: "shop_info",
      shop_name: shopName,
      kefu_name: kefuName
    });
  }

  function scanConversations(force) {
    const conversations = collectConversationEntries().map((entry) => entry.conversation);
    const fingerprint = JSON.stringify(conversations.map((item) => [item.display_name, item.unread_count]));
    if (!force && fingerprint === lastConversationFingerprint) return;
    lastConversationFingerprint = fingerprint;
    send({
      type: "conversation_snapshot",
      conversations
    });
  }

  function scanUnreadConversations(force) {
    const entries = collectConversationEntries().filter((entry) => entry.conversation.unread_count > 0);
    const unreadConversations = entries.map((entry) => entry.conversation);
    const fingerprint = JSON.stringify(unreadConversations.map((item) => [item.display_name, item.unread_count]));
    if (!force && fingerprint === lastUnreadFingerprint) return;
    lastUnreadFingerprint = fingerprint;
    if (unreadConversations.length) {
      console.info("[YY PDD agent] unread conversations", unreadConversations.map((item) => `${item.display_name}:${item.unread_count}`).join(", "));
    }
    send({
      type: "conversation_snapshot",
      conversations: unreadConversations
    });
    if (entries.length) {
      processUnreadConversation(entries[0]);
    }
  }

  function collectConversationEntries() {
    const nodes = firstNonEmptyNodes(CONVERSATION_SELECTORS).filter(isLikelyConversationNode).slice(0, CONVERSATION_LIMIT);
    const entries = nodes.map((node, index) => {
      const displayName = extractConversationName(node) || `conversation-${index + 1}`;
      const unreadNode = node.querySelector(".SessionBaseCard-unread-rot,[class*='unread']");
      const unreadText = textOf(unreadNode);
      const unreadCount = unreadText ? parseInt(unreadText, 10) || 1 : ((unreadNode || isUnreadConversationNode(node)) ? 1 : 0);
      if (!displayName || isConversationChromeText(displayName)) return null;
      return {
        node,
        conversation: {
          display_name: displayName,
          unread_count: unreadCount,
          confidence: displayName ? 75 : 45,
          raw: {
            selector: "conversation_candidates",
            index,
            text: textOf(node).slice(0, 500)
          }
        }
      };
    }).filter((item) => item && item.conversation && item.conversation.display_name);
    return uniqueConversationEntries(entries);
  }

  function uniqueConversationEntries(entries) {
    const seen = new Set();
    const result = [];
    for (const entry of entries) {
      const key = (entry.conversation.display_name || "").trim();
      if (!key || seen.has(key)) continue;
      seen.add(key);
      result.push(entry);
    }
    return result;
  }

  async function processUnreadConversation(entry) {
    if (!entry || !entry.node || unreadProcessing) return;
    const conversation = entry.conversation || {};
    const displayName = conversation.display_name || "";
    if (!displayName) return;
    const now = Date.now();
    const unreadKey = `${displayName}:${conversation.unread_count || 1}`;
    if (unreadKey === lastHandledUnreadKey && now - lastHandledUnreadAt < UNREAD_PROCESS_COOLDOWN_MS) {
      return;
    }
    unreadProcessing = true;
    lastHandledUnreadKey = unreadKey;
    lastHandledUnreadAt = now;
    try {
      const beforeName = currentConversationName();
      const clickResult = clickConversationNode(entry.node);
      await sleep(900);
      const afterName = currentConversationName();
      const selectedName = afterName || displayName;
      console.info(
        "[YY PDD agent] unread conversation selected",
        `target=${displayName}`,
        `before=${beforeName || "-"}`,
        `after=${afterName || "-"}`,
        `clickTarget=${clickResult.tag || "-"}`
      );
      await scanMessages(true, selectedName, "unread_switch");
    } finally {
      unreadProcessing = false;
    }
  }

  function uniqueConversations(conversations) {
    const seen = new Set();
    const result = [];
    for (const item of conversations) {
      const key = (item.display_name || "").trim();
      if (!key || seen.has(key)) continue;
      seen.add(key);
      result.push(item);
    }
    return result;
  }

  function isConversationChromeText(text) {
    return /今日接待|全部会话|批量转移|加载更多会话|智能快捷回复助手|手机端客服在线|待办任务/.test(text);
  }

  function isLikelyConversationNode(node) {
    const text = textOf(node).replace(/\s+/g, " ");
    if (!text || isConversationChromeText(text)) return false;
    if (node.matches(".chat-list-box li,[class*='chat-list'] li")) return true;
    if (node.matches(".SessionListGroupItem,[class*='SessionListGroupItem'],[class*='SessionBaseCard']")) return true;
    if (node.querySelector("[class*='unread'],[class*='avatar'],img[src]") && text.length <= 120) return true;
    return false;
  }

  function extractConversationName(node) {
    const explicitName = firstText([
      ".SessionBaseCard-topHeadName",
      "[class*='SessionBaseCard-topHeadName']",
      "[class*='topHeadName']",
      "[class*='buyerName']",
      "[class*='BuyerName']",
      "[class*='userName']",
      "[class*='UserName']",
      "[class*='nickname']",
      "[class*='NickName']",
      "[class*='name']"
    ], node);
    if (explicitName && explicitName !== lastKefuName && !isConversationChromeText(explicitName)) {
      return cleanConversationName(explicitName);
    }
    return cleanConversationName(textOf(node));
  }

  function cleanConversationName(text) {
    const value = (text || "").replace(/\s+/g, " ").trim();
    if (!value) return "";
    const tokens = value.split(" ").filter(Boolean);
    const statusWords = new Set(["已回复", "未回复", "待回复", "排队中", "接待中"]);
    for (let index = 0; index < tokens.length; index += 1) {
      if (statusWords.has(tokens[index]) && tokens[index + 1]) {
        return tokens[index + 1];
      }
    }
    for (const token of tokens) {
      if (statusWords.has(token)) continue;
      if (/^\d{1,2}:\d{2}$/.test(token)) continue;
      if (/转移会话|加载更多会话/.test(token)) continue;
      if (token !== lastKefuName) return token;
    }
    return "";
  }

  function currentConversationName() {
    const activeConversation = firstNonEmptyNodes([
      ".chat-list-box li.active",
      ".chat-list-box li.selected",
      ".chat-list-box li.current",
      ".chat-list-box li",
      "[class*='chat-list'] li.active",
      "[class*='chat-list'] li.selected",
      "[class*='chat-list'] li.current",
      "[class*='chat-list'] li"
    ]).map(extractConversationName).find(Boolean);
    if (activeConversation) return activeConversation;

    const explicitName = firstText([
      "[class*='buyer'] [class*='name']",
      "[class*='Buyer'] [class*='Name']",
      "[class*='user'] [class*='name']",
      "[class*='User'] [class*='Name']",
      "[id*='middlePanel'] [class*='name']",
      "[id*='rightPanel'] [class*='name']",
      ".SessionBaseCard-topHeadName",
      "[class*='topHeadName']"
    ]);
    return explicitName || inferConversationNameFromMessages() || "current";
  }

  function inferConversationNameFromMessages() {
    const nodes = firstNonEmptyNodes(MESSAGE_SELECTORS);
    for (const node of nodes) {
      const name = firstText([
        "[class*='buyerName']",
        "[class*='BuyerName']",
        "[class*='userName']",
        "[class*='UserName']",
        "[class*='nickname']",
        "[class*='NickName']"
      ], node);
      if (name && name !== lastKefuName) return name;
    }
    return "";
  }

  async function scanMessages(force, displayNameOverride = "", source = "manual") {
    let nodes = firstNonEmptyNodes(MESSAGE_SELECTORS);
    if (!nodes.length) {
      nodes = firstNonEmptyNodes(FALLBACK_MESSAGE_SELECTORS).filter(isLikelyChatMessageNode);
    }
    nodes = nodes.slice(-MESSAGE_LIMIT);
    const displayName = displayNameOverride || currentConversationName();
    const messages = nodes.map((node, index) => parseMessageNode(node, displayName, index)).filter(Boolean);
    const fingerprint = JSON.stringify(messages.map((item) => [
      displayName,
      item.time_text,
      item.sender_role,
      item.content_type,
      item.content,
      item.asset_url || ""
    ]));
    if (!force && fingerprint === lastMessageFingerprint) return;
    lastMessageFingerprint = fingerprint;
    await attachImageDataUrls(messages);
    send({
      type: "message_snapshot",
      display_name: displayName,
      source,
      messages
    });
  }

  async function attachImageDataUrls(messages) {
    for (const message of messages) {
      if (!message || message.content_type !== "image" || message.sender_role === "system") continue;
      if (!message.asset_url || message.asset_data_url) continue;
      try {
        message.asset_data_url = await fetchAssetAsDataUrl(message.asset_url);
        message.asset_capture_method = "fetch_data_url";
      } catch (error) {
        message.asset_fetch_error = String(error && error.message || error || "asset_fetch_failed").slice(0, 240);
      }
    }
  }

  async function fetchAssetAsDataUrl(url) {
    if (!url) throw new Error("empty_url");
    if (url.startsWith("data:")) return url;
    try {
      return await fetchAssetViaBackground(url);
    } catch (backgroundError) {
      try {
        return await fetchAssetInPage(url);
      } catch (pageError) {
        throw new Error(
          `background:${String(backgroundError && backgroundError.message || backgroundError)}; ` +
          `page:${String(pageError && pageError.message || pageError)}`
        );
      }
    }
  }

  async function fetchAssetViaBackground(url) {
    if (!globalThis.chrome || !chrome.runtime || !chrome.runtime.sendMessage) {
      throw new Error("runtime_unavailable");
    }
    return await new Promise((resolve, reject) => {
      chrome.runtime.sendMessage({ type: "fetch_image_data_url", url }, (response) => {
        const lastError = chrome.runtime.lastError;
        if (lastError) {
          reject(new Error(lastError.message || "runtime_last_error"));
          return;
        }
        if (!response || response.ok !== true || !response.data_url) {
          reject(new Error(response && response.error || "background_fetch_failed"));
          return;
        }
        resolve(String(response.data_url));
      });
    });
  }

  async function fetchAssetInPage(url) {
    const response = await fetch(url, { credentials: "include", cache: "force-cache" });
    if (!response.ok) throw new Error(`fetch_failed:${response.status}`);
    const blob = await response.blob();
    return await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result || ""));
      reader.onerror = () => reject(reader.error || new Error("file_reader_failed"));
      reader.readAsDataURL(blob);
    });
  }

  function hasLikelyMessageMarker(node) {
    if (!node) return false;
    if (node.matches("div[id*='cs-common-message-list-item'],[class*='LayoutDefaultWrapper']")) return true;
    return Boolean(node.querySelector([
      ".kwaishop-cs-BizTextCard",
      "[class*='BizTextCard']",
      ".image-msg",
      "[class*='image-msg']",
      "[class*='ImageMessage']",
      ".kwaishop-cs-BizOrderCard",
      "[class*='OrderCard']",
      "[class*='GoodsCard']",
      "[class*='BuyerFromCard']",
      "[class*='LayoutDefaultWrapper__time']",
      "[class*='isMine']",
      ".msg-system",
      "[class*='System']"
    ].join(",")));
  }

  function isEmptyStateNode(node) {
    const text = textOf(node).replace(/\s+/g, " ");
    return /请点击左侧会话|将窗口最大化|没有相关订单|暂无|暂无数据|暂无消息/.test(text);
  }

  function isLikelyChatMessageNode(node) {
    if (!node || isEmptyStateNode(node)) return false;
    if (hasLikelyMessageMarker(node)) return true;
    const text = textOf(node).replace(/\s+/g, "");
    if (text.length >= 2) return true;
    return false;
  }

  function backgroundUrl(node) {
    const value = node ? getComputedStyle(node).backgroundImage || "" : "";
    const match = value.match(/url\((['"]?)(.*?)\1\)/i);
    return match ? match[2] : "";
  }

  function sourceKind(url) {
    if (!url) return "none";
    if (url.startsWith("data:")) return "data_url";
    if (url.startsWith("blob:")) return "blob_url";
    if (/^https?:\/\//i.test(url)) return "remote_url";
    return "other_url";
  }

  function urlHost(url) {
    try {
      return new URL(url, location.href).hostname.toLowerCase();
    } catch (_error) {
      return "";
    }
  }

  function imageUrlOf(image) {
    return image.currentSrc || image.src || image.getAttribute("data-src") || image.getAttribute("data-original") || "";
  }

  function classChainOf(node, stopNode) {
    const parts = [];
    let current = node;
    while (current && current !== stopNode && parts.length < 6) {
      parts.push(classNameOf(current).toLowerCase());
      current = current.parentElement;
    }
    return parts.join(" ");
  }

  function isLikelyAvatarAsset(element, url, messageNode) {
    const host = urlHost(url);
    if (host === "savatar.pddpic.com") return true;
    const classes = classChainOf(element, messageNode);
    if (/(^|[-_\s])(avatar|head|portrait|usericon|user-icon)([-_\s]|$)/i.test(classes)) {
      return true;
    }
    return false;
  }

  function imageTargets(messageNode) {
    const targets = [];
    const seen = new Set();

    function addTarget(element, url) {
      if (!element) return;
      const assetUrl = url || "";
      const key = `${assetUrl}::${classNameOf(element)}`;
      if (seen.has(key)) return;
      seen.add(key);
      if (isLikelyAvatarAsset(element, assetUrl, messageNode)) return;
      targets.push({ element, asset_url: assetUrl });
    }

    const contentImages = Array.from(messageNode.querySelectorAll([
      ".image-msg img",
      "[class*='image-msg'] img",
      "[class*='ImageMessage'] img"
    ].join(",")));
    for (const image of contentImages) {
      addTarget(image, imageUrlOf(image));
    }

    if (!targets.length) {
      for (const image of Array.from(messageNode.querySelectorAll("img"))) {
        addTarget(image, imageUrlOf(image));
      }
    }

    const backgroundNodes = Array.from(messageNode.querySelectorAll("[style*='background-image'],[class*='image-msg'],[class*='ImageMessage']"));
    for (const backgroundNode of backgroundNodes) {
      const url = backgroundUrl(backgroundNode);
      if (url || !targets.length) {
        addTarget(backgroundNode, url);
      }
    }
    return targets;
  }

  function parseMessageNode(node, displayName, index) {
    if (!isLikelyChatMessageNode(node)) return null;
    const isMine = isAgentMessageNode(node);
    const systemNode = node.querySelector(".msg-system,[class*='System']");
    let contentType = "text";
    let content = "";
    let assetUrl = "";
    let assetSourceKind = "";

    const image = imageTargets(node)[0];
    const goods = node.querySelector(".good-card,[class*='GoodsCard']");
    const order = node.querySelector(".order-card,.kwaishop-cs-BizOrderCard,[class*='OrderCard']");
    const textNode = node.querySelector(".kwaishop-cs-BizTextCard,[class*='BizTextCard']");

    if (systemNode) {
      contentType = "notice";
      content = textOf(systemNode);
    } else if (image) {
      contentType = "image";
      assetUrl = image.asset_url || "";
      assetSourceKind = sourceKind(assetUrl);
      content = assetUrl ? "[image]" : textOf(node);
    } else if (goods) {
      contentType = "product";
      content = [
        firstText([".good-card .good-id", ".good-id", "[class*='good-id']"], goods),
        firstText([".good-card .good-name", ".good-name", "[class*='good-name']"], goods),
        textOf(goods)
      ].filter(Boolean)[0] || textOf(node);
    } else if (order) {
      contentType = "order";
      content = textOf(order);
    } else {
      content = textOf(textNode) || textOf(node);
      if (/当前用户来自|商品详情页|用户来自/.test(content)) {
        contentType = "lead";
      }
    }

    const timeText = firstText([
      ".kwaishop-cs-LayoutDefaultWrapper_sendItem.kwaishop-cs-LayoutDefaultWrapper__time",
      "[class*='LayoutDefaultWrapper__time']",
      "[class*='sendItem']"
    ], node) || extractTimeText(node);

    content = normalizeMessageContent(content, isMine);
    if (contentType === "lead" && !isLeadMessageContent(content)) {
      contentType = "text";
    }
    if (isAssistantNoticeContent(content)) {
      contentType = "notice";
    }

    if (!content && !assetUrl) return null;
    const senderRole = (systemNode || contentType === "notice") ? "system" : (isMine ? "agent" : "customer");
    return {
      display_name: displayName,
      sender_role: senderRole,
      is_self: isMine,
      sender_name: senderRole === "system" ? "system" : (isMine ? (lastKefuName || "agent") : displayName),
      content_type: contentType,
      content,
      asset_url: assetUrl,
      asset_source_kind: assetSourceKind,
      time_text: timeText,
      platform_msg_id: node.id || "",
      confidence: content ? 75 : 50,
      raw: {
        selector: "message_candidates",
        index,
        id: node.id || "",
        class_name: classNameOf(node),
        text: textOf(node).slice(0, 1000)
      }
    };
  }

  function isAgentMessageNode(node) {
    const className = classNameOf(node).toLowerCase();
    if (/(^|\s)(right|self|mine|me|send|sent|seller|service)(\s|$)|is[-_]?mine/.test(className)) {
      return true;
    }
    if (node.querySelector(".kwaishop-cs-LayoutDefaultWrapper__isMine,.cs-item,[class*='isMine']")) {
      return true;
    }
    const text = textOf(node).replace(/\s+/g, " ").trim();
    if (lastKefuName && text.startsWith(lastKefuName)) {
      return true;
    }
    return false;
  }

  function isLeadMessageContent(content) {
    return /当前用户来自|商品详情页|用户来自|褰撳墠鐢ㄦ埛鏉ヨ嚜|鍟嗗搧璇︽儏椤祙鐢ㄦ埛鏉ヨ嚜/.test(content || "");
  }

  function isAssistantNoticeContent(content) {
    return /智能快捷回复助手|常见问题回答|立即配置|消费者问到|接待效率|减少顾客流失|建议您尽快|配置消费者/.test(content || "");
  }

  function extractTimeText(node) {
    const text = textOf(node).replace(/\s+/g, " ");
    const match = text.match(/\d{4}[-/年]\d{1,2}[-/月]\d{1,2}[日]?\s+\d{1,2}:\d{2}(?::\d{2})?/);
    return match ? match[0] : "";
  }

  function normalizeMessageContent(content, isMine) {
    let value = (content || "").replace(/\s+/g, " ").trim();
    value = value.replace(/^\d{4}[-/年]\d{1,2}[-/月]\d{1,2}[日]?\s+\d{1,2}:\d{2}(?::\d{2})?\s*/, "");
    if (isMine && lastKefuName && value.startsWith(lastKefuName)) {
      value = value.slice(lastKefuName.length).trim();
    }
    return value;
  }

  function reportSelectorDebug(force) {
    const now = Date.now();
    if (!force && now - lastDebugAt < DEBUG_INTERVAL_MS) return;
    lastDebugAt = now;
    const payload = {
      type: "debug_snapshot",
      frame_url: location.href,
      title: document.title,
      ready_state: document.readyState,
      iframe_count: document.querySelectorAll("iframe").length,
      conversation_selector_hits: countSelectorHits(CONVERSATION_SELECTORS),
      message_selector_hits: countSelectorHits([
        ...MESSAGE_SELECTORS,
        ...FALLBACK_MESSAGE_SELECTORS,
        "#replyTextarea",
        "#replyText"
      ]),
      panel_selector_hits: countSelectorHits(PANEL_DEBUG_SELECTORS),
      conversation_selector_samples: sampleSelectorNodes(CONVERSATION_SELECTORS),
      message_selector_samples: sampleSelectorNodes([
        ...MESSAGE_SELECTORS,
        ...FALLBACK_MESSAGE_SELECTORS,
        "#replyTextarea",
        "#replyText"
      ]),
      panel_selector_samples: sampleSelectorNodes(PANEL_DEBUG_SELECTORS, 2)
    };
    console.debug("[YY PDD agent] selector debug", payload);
    send(payload);
  }

  async function prepareReplyDraft(command) {
    const text = command.text || "";
    const contentType = command.content_type || "text";
    const targetName = command.display_name || "";
    let currentName = currentConversationName();
    let selectedResult = null;
    if (command.prefer_unread || command.select_conversation_before_draft) {
      const selected = await selectConversationForDraft(
        targetName,
        Boolean(command.prefer_unread),
        command.switch_unread_method || "click"
      );
      selectedResult = selected;
      if (!selected.ok) {
        send({
          type: "draft_result",
          request_id: command.request_id,
          prepared: false,
          status: "error",
          error: selected.error,
          reason: selected.reason || "",
          display_name: currentName,
          conversation_key: command.conversation_key || ""
        });
        return;
      }
      currentName = selected.display_name || currentConversationName();
    }
    if (command.require_target_verification && targetName && currentName && targetName !== currentName) {
      send({
        type: "draft_result",
        request_id: command.request_id,
        prepared: false,
        status: "error",
        error: "target_conversation_mismatch",
        reason: `current=${currentName}, target=${targetName}`,
        display_name: currentName,
        conversation_key: command.conversation_key || ""
      });
      return;
    }

    const input = findReplyInput();
    if (!input) {
      send({
        type: "draft_result",
        request_id: command.request_id,
        prepared: false,
        status: "error",
        error: "reply_input_missing",
        display_name: currentName,
        conversation_key: command.conversation_key || ""
      });
      return;
    }

    input.focus();
    let imagePasteResult = null;
    if (contentType === "image") {
      imagePasteResult = await pasteImageDataUrl(input, command.image_data_url || "", command.file_name || "image.png");
      if (!imagePasteResult.ok) {
        send({
          type: "draft_result",
          request_id: command.request_id,
          prepared: false,
          status: "error",
          error: imagePasteResult.error || "image_paste_failed",
          reason: imagePasteResult.reason || "",
          display_name: currentName,
          conversation_key: command.conversation_key || ""
        });
        return;
      }
    } else {
      if ("value" in input) {
        input.value = text;
      } else {
        input.textContent = text;
      }
      input.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertText", data: text }));
      input.dispatchEvent(new Event("change", { bubbles: true }));
    }
    let enterResult = null;
    if (command.allow_send_enter) {
      enterResult = dispatchEnterToSend(input);
      await sleep(250);
    }
      send({
        type: "draft_result",
        request_id: command.request_id,
        prepared: true,
        sent: Boolean(command.allow_send_enter),
        status: "success",
        reason: [
          selectedResult && selectedResult.reason ? selectedResult.reason : "",
          imagePasteResult ? `image_paste=${imagePasteResult.method}` : "",
          enterResult ? `send_method=enter, keydown=${enterResult.keydown}, keypress=${enterResult.keypress}, keyup=${enterResult.keyup}` : ""
        ].filter(Boolean).join("; "),
        display_name: currentName,
        conversation_key: command.conversation_key || ""
      });
  }

  async function selectConversationForDraft(targetName, preferUnread, switchMethod) {
    const beforeName = currentConversationName();
    if (preferUnread && switchMethod !== "click") {
      dispatchShiftTabShortcut();
      await sleep(800);
      const afterShortcutName = currentConversationName();
      if (afterShortcutName && afterShortcutName !== beforeName) {
        return {
          ok: true,
          error: "",
          display_name: afterShortcutName,
          reason: `method=shortcut, before=${beforeName || "-"}, after=${afterShortcutName}`
        };
      }
      const unreadNode = findUnreadConversationNode();
      const unreadName = unreadNode ? extractConversationName(unreadNode) : "";
      if (preferUnread && unreadName && (!targetName || unreadName === targetName || unreadName === afterShortcutName)) {
        return {
          ok: true,
          error: "",
          display_name: unreadName,
          reason: `method=shortcut, unread=${unreadName}, before=${beforeName || "-"}, after=${afterShortcutName || "-"}`
        };
      }
      if (switchMethod === "shortcut") {
        return {
          ok: false,
          error: "shortcut_no_visible_switch",
          reason: `method=shortcut, before=${beforeName || "-"}, after=${afterShortcutName || "-"}`
        };
      }
    }

    let node = null;
    if (preferUnread) {
      node = findUnreadConversationNode();
    }
    if (!node && targetName) {
      node = findConversationNodeByName(targetName);
    }
    if (!node) {
      return {
        ok: !preferUnread && !targetName,
        error: "conversation_not_found",
        reason: `prefer_unread=${preferUnread}, target=${targetName || "-"}`
      };
    }
    const clickResult = clickConversationNode(node);
    await sleep(800);
    const afterName = currentConversationName();
    const nodeName = extractConversationName(node);
    const ok = Boolean(afterName) && (!targetName || afterName === targetName || nodeName === afterName);
    return {
      ok,
      error: ok ? "" : "target_conversation_mismatch",
      display_name: afterName,
      reason: [
        "method=click",
        `ok=${ok}`,
        `before=${beforeName || "-"}`,
        `after=${afterName || "-"}`,
        `target=${targetName || nodeName || "-"}`,
        `node=${nodeName || "-"}`,
        `clickTarget=${clickResult.tag || "-"}`,
        `rect=${clickResult.rect || "-"}`,
        `text=${clickResult.text || "-"}`
      ].join(", ")
    };
  }

  async function pasteImageDataUrl(input, dataUrl, fileName) {
    if (!dataUrl || !dataUrl.startsWith("data:image/")) {
      return { ok: false, error: "image_data_url_missing", reason: "image data url is empty or invalid" };
    }
    if (typeof DataTransfer === "undefined" || typeof File === "undefined") {
      return { ok: false, error: "browser_image_paste_unsupported", reason: "DataTransfer/File API unavailable" };
    }
    try {
      const response = await fetch(dataUrl);
      const blob = await response.blob();
      const safeFileName = fileName || `image-${Date.now()}.png`;
      const file = new File([blob], safeFileName, { type: blob.type || "image/png" });
      const transfer = new DataTransfer();
      transfer.items.add(file);
      let pasteEvent = null;
      try {
        pasteEvent = new ClipboardEvent("paste", {
          bubbles: true,
          cancelable: true,
          composed: true,
          clipboardData: transfer
        });
      } catch (_error) {
        pasteEvent = new Event("paste", { bubbles: true, cancelable: true, composed: true });
      }
      if (!pasteEvent.clipboardData || pasteEvent.clipboardData.files.length === 0) {
        try {
          Object.defineProperty(pasteEvent, "clipboardData", { value: transfer });
        } catch (_error) {
          return { ok: false, error: "clipboard_data_bind_failed", reason: "failed to bind DataTransfer to paste event" };
        }
      }
      const accepted = input.dispatchEvent(pasteEvent);
      input.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertFromPaste" }));
      input.dispatchEvent(new Event("change", { bubbles: true }));
      await sleep(800);
      console.info("[YY PDD agent] image paste dispatched", {
        accepted,
        file_name: safeFileName,
        mime_type: file.type,
        bytes: file.size
      });
      return { ok: true, method: `paste_event:${accepted ? "accepted" : "default_prevented"}` };
    } catch (error) {
      return {
        ok: false,
        error: "image_paste_exception",
        reason: error && error.message ? error.message : String(error)
      };
    }
  }

  function clickConversationNode(node) {
    const clickTarget = resolveConversationClickTarget(node);
    clickTarget.scrollIntoView({ block: "center", inline: "nearest" });
    const rect = clickTarget.getBoundingClientRect();
    const x = Math.max(1, Math.min(window.innerWidth - 1, rect.left + Math.min(rect.width / 2, 80)));
    const y = Math.max(1, Math.min(window.innerHeight - 1, rect.top + rect.height / 2));
    const eventInit = {
      bubbles: true,
      cancelable: true,
      composed: true,
      view: window,
      clientX: x,
      clientY: y,
      screenX: window.screenX + x,
      screenY: window.screenY + y,
      button: 0,
      buttons: 1
    };
    try {
      clickTarget.focus && clickTarget.focus({ preventScroll: true });
    } catch (_error) {
      // Ignore focus failures.
    }
    for (const type of ["pointerover", "pointerenter", "mouseover", "mouseenter", "pointerdown", "mousedown"]) {
      dispatchPointerLikeEvent(clickTarget, type, eventInit);
    }
    for (const type of ["pointerup", "mouseup", "click"]) {
      dispatchPointerLikeEvent(clickTarget, type, { ...eventInit, buttons: 0 });
    }
    try {
      clickTarget.click && clickTarget.click();
    } catch (_error) {
      // Ignore native click failures.
    }
    const summary = {
      tag: clickTarget.tagName ? clickTarget.tagName.toLowerCase() : "",
      rect: `${Math.round(rect.left)},${Math.round(rect.top)},${Math.round(rect.width)}x${Math.round(rect.height)}`,
      text: textOf(clickTarget).replace(/\s+/g, " ").trim().slice(0, 80)
    };
    console.debug("[YY PDD agent] conversation click", summary);
    return summary;
  }

  function dispatchPointerLikeEvent(target, type, eventInit) {
    const eventClass = type.startsWith("pointer") && typeof PointerEvent !== "undefined"
      ? PointerEvent
      : MouseEvent;
    try {
      target.dispatchEvent(new eventClass(type, eventInit));
    } catch (_error) {
      target.dispatchEvent(new MouseEvent(type, eventInit));
    }
  }

  function resolveConversationClickTarget(node) {
    const candidates = [
      node.querySelector("[role='button']"),
      node.querySelector("button"),
      node.querySelector("a"),
      node.querySelector("[class*='item']"),
      node,
      node.closest("li"),
      node.closest("[class*='chat-list']"),
      node.parentElement
    ].filter(Boolean);
    for (const candidate of candidates) {
      const rect = candidate.getBoundingClientRect();
      if (rect.width > 0 && rect.height > 0) return candidate;
    }
    return node;
  }

  function dispatchShiftTabShortcut() {
    const target = document.activeElement || document.body || document.documentElement;
    if (target && typeof target.blur === "function" && isReplyInput(target)) {
      target.blur();
    }
    const eventInit = {
      key: "Tab",
      code: "Tab",
      keyCode: 9,
      which: 9,
      shiftKey: true,
      bubbles: true,
      cancelable: true,
      composed: true
    };
    for (const eventTarget of [target, document, window]) {
      try {
        eventTarget.dispatchEvent(new KeyboardEvent("keydown", eventInit));
        eventTarget.dispatchEvent(new KeyboardEvent("keyup", eventInit));
      } catch (_error) {
        // Ignore dispatch failures on non-Element targets.
      }
    }
  }

  function dispatchEnterToSend(input) {
    try {
      input.focus();
    } catch (_error) {
      // Ignore focus failures.
    }
    const eventInit = {
      key: "Enter",
      code: "Enter",
      keyCode: 13,
      which: 13,
      charCode: 13,
      bubbles: true,
      cancelable: true,
      composed: true
    };
    const targets = [input, document, window].filter(Boolean);
    const result = { keydown: false, keypress: false, keyup: false };
    for (const type of ["keydown", "keypress", "keyup"]) {
      for (const target of targets) {
        try {
          const accepted = target.dispatchEvent(new KeyboardEvent(type, eventInit));
          result[type] = result[type] || accepted;
        } catch (_error) {
          // Ignore dispatch failures on non-Element targets.
        }
      }
    }
    console.info("[YY PDD agent] enter dispatched for send", result);
    return result;
  }

  function findConversationNodes() {
    return firstNonEmptyNodes(CONVERSATION_SELECTORS).filter(isLikelyConversationNode);
  }

  function findConversationNodeByName(displayName) {
    return findConversationNodes().find((node) => extractConversationName(node) === displayName) || null;
  }

  function findUnreadConversationNode() {
    return findConversationNodes().find(isUnreadConversationNode) || null;
  }

  function isUnreadConversationNode(node) {
    const text = textOf(node).replace(/\s+/g, " ");
    if (/未回复|待回复|请\d*分钟内回复|超时|红点|new/i.test(text)) return true;
    if (node.querySelector("[class*='unread'],[class*='red'],[class*='dot'],[class*='badge']")) return true;
    return Boolean(node.querySelector(".ant-badge-dot,.ant-badge-count,.semi-badge,.semi-badge-dot"));
  }

  function findReplyInput() {
    return document.querySelector("#replyTextarea")
      || document.querySelector("#replyText")
      || document.querySelector("[contenteditable='true'][role='textbox']")
      || document.querySelector("[contenteditable='true']")
      || document.querySelector("textarea");
  }

  function isReplyInput(node) {
    if (!node || !node.matches) return false;
    return node.matches("#replyTextarea,#replyText,textarea,[contenteditable='true']");
  }

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  if (isTargetChatPage()) {
    connect();
  } else {
    console.info("[YY PDD agent] skipped non-chat page", location.href);
  }
})();
