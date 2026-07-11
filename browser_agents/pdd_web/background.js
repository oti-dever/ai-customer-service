const MAX_IMAGE_BYTES = 8 * 1024 * 1024;

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (!message || message.type !== "fetch_image_data_url") {
    return false;
  }

  (async () => {
    try {
      const dataUrl = await fetchImageDataUrl(String(message.url || ""));
      sendResponse({ ok: true, data_url: dataUrl });
    } catch (error) {
      sendResponse({
        ok: false,
        error: String(error && error.message || error || "fetch_image_failed").slice(0, 500)
      });
    }
  })();
  return true;
});

async function fetchImageDataUrl(url) {
  if (!url) {
    throw new Error("empty_url");
  }
  if (url.startsWith("data:")) {
    return url;
  }

  const parsed = new URL(url);
  if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
    throw new Error(`unsupported_scheme:${parsed.protocol}`);
  }

  const response = await fetch(url, { credentials: "include", cache: "force-cache" });
  if (!response.ok) {
    throw new Error(`fetch_failed:${response.status}`);
  }

  const contentType = (response.headers.get("content-type") || "application/octet-stream").split(";")[0].trim();
  if (contentType && !contentType.startsWith("image/")) {
    throw new Error(`not_image:${contentType}`);
  }

  const buffer = await response.arrayBuffer();
  if (buffer.byteLength > MAX_IMAGE_BYTES) {
    throw new Error(`image_too_large:${buffer.byteLength}`);
  }

  return `data:${contentType || "application/octet-stream"};base64,${arrayBufferToBase64(buffer)}`;
}

function arrayBufferToBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  const chunkSize = 0x8000;
  for (let index = 0; index < bytes.length; index += chunkSize) {
    const chunk = bytes.subarray(index, index + chunkSize);
    binary += String.fromCharCode.apply(null, chunk);
  }
  return btoa(binary);
}
