<template>
  <div id="multiopus">
    <div class="wrapper">
      <!-- 头部：标题 + 中英文切换（选择持久化到 localStorage） -->
      <div class="header">
        <span class="title">{{ m.appTitle }} · Live Demo 1</span>
        <div class="header-right">
          <a class="docs-link" href="?demo=2">Live Demo 2</a>
          <a class="docs-link" href="docs/" target="_blank" rel="noopener">{{ m.docs }}</a>
          <n-button size="small" @click="toggleLocale">
            {{ locale === "zh" ? "EN" : "中文" }}
          </n-button>
        </div>
      </div>
      <div class="content input">
        <div class="input-item" style="flex: 1">
          <span>{{ m.channel }}</span>
          <n-input v-model:value="channel" />
        </div>
        <div class="input-item" style="flex: 1">
          <span>{{ m.targetUid }}</span>
          <n-input v-model:value="targetUid" :placeholder="m.targetUidPh" />
        </div>
      </div>
      <div class="content input">
        <div class="input-item" style="flex: 1">
          <span>{{ m.myUid }}</span>
          <n-input v-model:value="myUid" :placeholder="m.myUidPh" />
        </div>
        <div class="input-item" style="flex: 1">
          <span>{{ m.appId }}</span>
          <n-input v-model:value="appId" :placeholder="m.appIdPh" />
        </div>
      </div>
      <div class="content input">
        <div class="input-item" style="flex: 1">
          <span>{{ m.token }}</span>
          <n-input v-model:value="token" :placeholder="m.tokenPh" />
        </div>
      </div>

      <!-- 发布参数：多声道 WAV 素材 + 声道数 -->
      <div class="content input">
        <div class="input-item" style="flex: 2">
          <span>{{ m.wavToPublish }}</span>
          <n-select v-model:value="audioURL" :options="audioURLOptions" />
        </div>
        <div class="input-item" style="flex: 1">
          <span>{{ m.channelCount }}</span>
          <n-select v-model:value="channelCount" :options="channelCountOptions" />
        </div>
      </div>

      <!-- 角色链接：新标签页分别以 Publisher / Subscriber 身份打开 -->
      <div class="content links">
        <a :href="publisherLink" target="_blank">{{ m.openPublisher }} (uid: {{ myUid }})</a>
        <a :href="subscriberLink" target="_blank"
          >{{ m.openSubscriber }} (uid: {{ targetUid }} → subTo: {{ myUid }})</a
        >
      </div>

      <div class="content operation">
        <n-button type="info" @click="joinChannel" :disabled="joined">{{ m.join }}</n-button>
        <n-button type="primary" @click="publishAudio" :disabled="!joined || published"
          >{{ m.publish }}</n-button
        >
        <n-button @click="unpublishAudio" :disabled="!published">{{ m.unpublish }}</n-button>
        <n-button type="success" @click="subscribe" :disabled="!joined || subscribed"
          >{{ m.subscribeCheck }}</n-button
        >
        <n-button @click="leaveChannel" :disabled="!joined">{{ m.leave }}</n-button>
      </div>

      <!-- 声道选择：先订阅，再选择声道组合与渲染模式 -->
      <div class="content input">
        <div class="input-item" style="flex: 2">
          <span>{{ m.channelsLabel }}</span>
          <n-input v-model:value="channelsInput" :placeholder="m.channelsPh" />
        </div>
        <div class="input-item" style="flex: 3">
          <n-button size="small" @click="selectChannels('surround')" :disabled="!subscribed"
            >surround</n-button
          >
          <n-button size="small" @click="selectChannels('stereo')" :disabled="!subscribed"
            >{{ m.stereoBtn }}</n-button
          >
          <n-button size="small" @click="selectChannels('mono')" :disabled="!subscribed"
            >{{ m.monoBtn }}</n-button
          >
        </div>
      </div>

      <!-- 检测结果面板 -->
      <div class="content result" :class="resultClass">
        <div class="verdict">
          <span class="r-label">{{ m.result }}:</span>
          <span class="r-value">{{ verdictText }}</span>
        </div>
        <div class="meta">codec: {{ codecText }}</div>
        <div class="meta" v-if="remoteChannelCount">
          {{ m.remoteCc }}: {{ remoteChannelCount }}
        </div>
        <div class="bars" v-if="volumes.length">
          <div class="bar-row" v-for="(v, i) in volumes" :key="i">
            <span class="ch">CH{{ i }}</span>
            <div class="outer">
              <div class="inner" :style="{ width: `${Math.round(v * 100)}%` }"></div>
            </div>
            <span class="pct">{{ Math.round(v * 100) }}%</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import AgoraRTC from "agora-rtc-sdk-ng";
import type {
  IAgoraRTCClient,
  ILocalAudioTrack,
  IRemoteAudioTrack,
} from "agora-rtc-sdk-ng";

import { NButton, NInput, NSelect } from "naive-ui";
import { ref, computed, watch, onBeforeUnmount } from "vue";
import { useNotification } from "naive-ui";

const notification = useNotification();

// ---------------- 中英文切换 ----------------
// 默认按浏览器语言，用户切换后持久化到 localStorage。
const locale = ref<"zh" | "en">(
  (localStorage.getItem("locale") as "zh" | "en") ||
    (navigator.language.toLowerCase().startsWith("zh") ? "zh" : "en")
);
function toggleLocale() {
  locale.value = locale.value === "zh" ? "en" : "zh";
}

const messages = {
  zh: {
    appTitle: "Agora 多声道音频验证 Demo",
    docs: "文档",
    channel: "频道",
    targetUid: "目标 UID (subTo)",
    targetUidPh: "要订阅检测的那条流的 uid",
    myUid: "本端 UID",
    myUidPh: "加入用的 uid（与 token 绑定）",
    appId: "App ID",
    appIdPh: "Agora 项目的 App ID",
    token: "Token",
    tokenPh: "本端 uid 对应的 token（无证书项目可留空）",
    wavToPublish: "发布素材",
    channelCount: "声道数",
    openPublisher: "打开发布端",
    openSubscriber: "打开订阅端",
    join: "加入频道",
    publish: "发布音频",
    unpublish: "停止发布",
    subscribeCheck: "订阅检测",
    leave: "离开",
    channelsLabel: "声道选择（逗号分隔）",
    channelsPh: "例如 0,1 或 0,2,4,6",
    stereoBtn: "stereo（0,2,4,6→左 / 1,3,5,7→右）",
    monoBtn: "mono（全部混单声道）",
    result: "检测结果",
    notSubscribed: "尚未订阅",
    detecting: "检测中…",
    multiChannel: "多声道 ✓",
    mono: "单声道 ✗",
    chUnit: "声道",
    codecMulti: "多声道编码",
    codecOpus: "普通编码，非多声道",
    remoteCc: "远端音轨 channelCount（信令）",
    joined: "加入成功",
    leftChannel: "已离开频道",
    enterChannels: "请输入声道序号，如 0,1",
    channelOutOfRange: "声道序号必须在 0~7 之间",
    wavLabel: (n: number) => `${n}_Channel_ID.wav（${n} 声道）`,
    publishedOk: (n: number) => `已发布 ${n} 声道音频`,
    publishFail: (msg: string) => `发布失败: ${msg}`,
    streamNotFound: (uid: string) => `频道内未找到 uid ${uid} 的流`,
    noAudio: (uid: string) => `uid ${uid} 尚未发布音频`,
  },
  en: {
    appTitle: "Agora Multi-Channel Audio Demo",
    docs: "Docs",
    channel: "Channel",
    targetUid: "Target UID (subTo)",
    targetUidPh: "uid of the stream to check",
    myUid: "Local UID",
    myUidPh: "join uid, bound to token",
    appId: "App ID",
    appIdPh: "App ID of your Agora project",
    token: "Token",
    tokenPh: "token for local uid, leave empty if no certificate",
    wavToPublish: "WAV to publish",
    channelCount: "Channel count",
    openPublisher: "Open Publisher",
    openSubscriber: "Open Subscriber",
    join: "Join",
    publish: "Publish",
    unpublish: "Unpublish",
    subscribeCheck: "Subscribe & Check",
    leave: "Leave",
    channelsLabel: "Channels (comma-separated)",
    channelsPh: "e.g. 0,1 or 0,2,4,6",
    stereoBtn: "stereo (0,2,4,6→L / 1,3,5,7→R)",
    monoBtn: "mono (mix all to mono)",
    result: "Result",
    notSubscribed: "Not subscribed",
    detecting: "Detecting…",
    multiChannel: "Multi-channel ✓",
    mono: "Mono ✗",
    chUnit: "ch",
    codecMulti: "multi-channel codec",
    codecOpus: "standard codec, not multi-channel",
    remoteCc: "Remote track channelCount (signaling)",
    joined: "Joined",
    leftChannel: "Left channel",
    enterChannels: "Enter channel indices, e.g. 0,1",
    channelOutOfRange: "Channel indices must be between 0 and 7",
    wavLabel: (n: number) => `${n}_Channel_ID.wav (${n}ch)`,
    publishedOk: (n: number) => `Published ${n}ch audio`,
    publishFail: (msg: string) => `Publish failed: ${msg}`,
    streamNotFound: (uid: string) => `Stream uid ${uid} not found in channel`,
    noAudio: (uid: string) => `uid ${uid} has not published audio`,
  },
};
const m = computed(() => messages[locale.value]);

// 语言切换联动：持久化选择，并同步 <html lang> 与页面 title（标签页标题）。
watch(
  locale,
  (v) => {
    localStorage.setItem("locale", v);
    document.documentElement.lang = v === "zh" ? "zh-CN" : "en";
    document.title = `${m.value.appTitle} (multiopus)`;
  },
  { immediate: true }
);

// 开启多声道（multiopus）编解码协商能力：只有开了，订阅端才可能收到 4/6/8 声道的
// 多声道流，发布端才能以 OPUSMC 编码发出多声道；不开则一律按普通单/双声道处理。
// 注意：必须在 AgoraRTC.createClient() 之前设置，创建 client 后再设置不生效。
//@ts-ignore
AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true);

// 演示用 App ID 默认值（明文 demo 凭证，仅供本地验证；生产环境请在页面内
// 替换为自己的 App ID，并从自己的服务端动态获取 token）。
const DEFAULT_APP_ID = "bbae89fd0cb14412883a3adf1ee57e4a";

// 钩住 RTCPeerConnection，以便从底层 getStats() 读取实际收到的 codec
// （opus = 普通单/双声道，multiopus = 多声道）。Agora SDK 不暴露内部 PC，
// 这里在 client 创建前包一层、收集所有实例，仅用于本检测工具。
//@ts-ignore
if (!window.__agoraPcHooked) {
  //@ts-ignore
  const OriginalPC = window.RTCPeerConnection;
  //@ts-ignore
  window.__agoraPcs = [];
  //@ts-ignore
  class HookedPC extends OriginalPC {
    constructor(...args: any[]) {
      super(...args);
      //@ts-ignore
      window.__agoraPcs.push(this);
    }
  }
  //@ts-ignore
  window.RTCPeerConnection = HookedPC;
  //@ts-ignore
  window.__agoraPcHooked = true;
}

const urlParams = new URLSearchParams(location.search);

// 频道 / 本端 uid(+token) / 目标 uid 持久化到 localStorage，下次自动回填。
// token 与 uid 绑定：本端必须用「本端 uid」对应的 token 加入。
// URL 参数（channel / uid / token / subTo）优先于 localStorage，用于双标签页角色控制。
const channel = ref(
  urlParams.get("channel") || localStorage.getItem("channelName") || "191568709962"
);
const myUid = ref(urlParams.get("uid") || localStorage.getItem("myUid") || "479911");
const token = ref(urlParams.get("token") || localStorage.getItem("token") || "");
const targetUid = ref(
  urlParams.get("subTo") || localStorage.getItem("targetUid") || "336983"
);
const appId = ref(urlParams.get("appId") || localStorage.getItem("appId") || DEFAULT_APP_ID);

watch(channel, (v) => localStorage.setItem("channelName", v));
watch(myUid, (v) => localStorage.setItem("myUid", v));
watch(token, (v) => localStorage.setItem("token", v));
watch(targetUid, (v) => localStorage.setItem("targetUid", v));
watch(appId, (v) => localStorage.setItem("appId", v));

// 双标签页角色链接：发布端以 myUid 加入；订阅端以 targetUid 加入并订阅 myUid。
// 链接不带 token（token 与 uid 绑定，需在各标签页内手动填入；uid=0 通配 token 可共用）。
// 所有查询参数均 encodeURIComponent，防止特殊字符破坏 URL 或被注入。
const publisherLink = computed(
  () =>
    `${location.origin}${location.pathname}?channel=${encodeURIComponent(
      channel.value
    )}&uid=${encodeURIComponent(myUid.value)}`
);
const subscriberLink = computed(
  () =>
    `${location.origin}${location.pathname}?channel=${encodeURIComponent(
      channel.value
    )}&uid=${encodeURIComponent(targetUid.value)}&subTo=${encodeURIComponent(myUid.value)}`
);

// ---------------- 发布端：多声道 WAV → 自定义音轨 ----------------
// 素材 URL 带 base 前缀，兼容子路径部署（如 /multiopus/）；追加 __BUILD_ID__ 用于前端缓存管理
const wavBase = import.meta.env.BASE_URL;
const wavUrl = (n: number) => `${wavBase}${n}_Channel_ID.wav?v=${__BUILD_ID__}`;
const audioURLOptions = computed(() =>
  [4, 6, 8].map((n) => ({
    label: m.value.wavLabel(n),
    value: wavUrl(n),
  }))
);
const channelCountOptions = [
  { label: "4", value: 4 },
  { label: "6", value: 6 },
  { label: "8", value: 8 },
];
const audioURL = ref(wavUrl(8));
const channelCount = ref(8);

// 切换素材时联动声道数（素材文件名即声道数）。
watch(audioURL, (v) => {
  const m2 = v.match(/(\d+)_Channel_ID/);
  if (m2) channelCount.value = Number(m2[1]);
});

let audioContext: AudioContext | null = null;
let localAudioTrack: ILocalAudioTrack | null = null;
const published = ref(false);

// fetch 多声道 WAV 并用 Web Audio 解码为 AudioBuffer。
async function createAudioBuffer(url: string): Promise<AudioBuffer> {
  const res = await fetch(url);
  const arr = await res.arrayBuffer();
  audioContext = audioContext || new AudioContext();
  return await audioContext.decodeAudioData(arr);
}

// AudioBuffer → MediaStreamDestination（显式 channelCount，避免被 down-mix 成立体声）
// → MediaStreamTrack。source 播放结束后重新挂载实现循环播放。
function getAudioBufferTrack(buffer: AudioBuffer): MediaStreamTrack {
  audioContext = audioContext || new AudioContext();
  const dest = audioContext.createMediaStreamDestination();
  dest.channelCount = channelCount.value;
  dest.channelCountMode = "explicit";
  dest.channelInterpretation = "discrete";
  const startSource = () => {
    const source = audioContext!.createBufferSource();
    source.buffer = buffer;
    source.connect(dest);
    source.onended = () => {
      source.disconnect();
      startSource();
    };
    source.start();
  };
  startSource();
  return dest.stream.getAudioTracks()[0];
}

async function publishAudio() {
  if (!client || !joined.value || published.value) return;
  try {
    if (audioContext && audioContext.state === "suspended") await audioContext.resume();
    const buffer = await createAudioBuffer(audioURL.value);
    const mediaStreamTrack = getAudioBufferTrack(buffer);
    // channelCount 驱动 SDK 生成发布用的 custom_payload_type（OPUSMC）。
    localAudioTrack = await AgoraRTC.createCustomAudioTrack({
      mediaStreamTrack,
      channelCount: channelCount.value,
    });
    await client.publish(localAudioTrack);
    published.value = true;
    notification.success({
      title: m.value.publishedOk(channelCount.value),
      duration: 2000,
    });
  } catch (e: any) {
    notification.error({ title: m.value.publishFail(e?.message || String(e)), duration: 3000 });
    // 发布失败时关闭 AudioContext，避免泄漏和循环播放残留。
    await audioContext?.close();
    audioContext = null;
  }
}

async function unpublishAudio() {
  if (!client || !localAudioTrack) return;
  await client.unpublish(localAudioTrack);
  localAudioTrack.close();
  localAudioTrack = null;
  published.value = false;
  // 关闭 AudioContext 停掉循环播放的 source，下次发布时重建。
  await audioContext?.close();
  audioContext = null;
}

// ---------------- 订阅端：订阅检测 / 声道选择 / 逐声道音量 ----------------
let client: IAgoraRTCClient | null = null;
const joined = ref(false);
const subscribed = ref(false);

const volumes = ref<number[]>([]);
let volumeTimer = -1;

const codec = ref(""); // 'opus' | 'multiopus' | ''
const codecChannels = ref(0); // getStats 里 codec 的声道数（opus 在 SDP 里恒为 2，仅参考）
let codecTimer = -1;

let remoteAudioTrack: IRemoteAudioTrack | null = null;
const remoteChannelCount = ref(0);

const channelsInput = ref("0,1");

// 主判定信号：getVolumeLevels() 数组长度 = 实际解出的声道数。
const verdictText = computed(() => {
  if (!subscribed.value) return m.value.notSubscribed;
  if (volumes.value.length === 0) return m.value.detecting;
  const n = volumes.value.length;
  const multi = codec.value === "multiopus" || [4, 6, 8].includes(n);
  return `${n} ${m.value.chUnit} → ${multi ? m.value.multiChannel : m.value.mono}`;
});

const isMulti = computed(
  () => subscribed.value && (codec.value === "multiopus" || [4, 6, 8].includes(volumes.value.length))
);

const resultClass = computed(() => {
  if (!subscribed.value || volumes.value.length === 0) return "";
  return isMulti.value ? "is-multi" : "is-mono";
});

const codecText = computed(() => {
  if (codec.value === "multiopus")
    return `multiopus (${m.value.codecMulti}${
      codecChannels.value ? ", " + codecChannels.value + " " + m.value.chUnit : ""
    })`;
  // opus 的 SDP 声道数恒为 2，不代表内容是立体声，因此不展示该数字以免误读。
  if (codec.value === "opus") return `opus (${m.value.codecOpus})`;
  if (codec.value) return codec.value;
  return subscribed.value ? m.value.detecting : "—";
});

async function joinChannel() {
  if (joined.value) return;
  client = AgoraRTC.createClient({ mode: "rtc", codec: "vp8" });

  await client.join(
    appId.value,
    channel.value,
    token.value || null,
    Number(myUid.value) || null
  );
  joined.value = true;
  notification.success({ title: m.value.joined, duration: 1000 });

  client.on("user-published", (user, mediaType) => {
    console.warn("user-published", user.uid, mediaType);
  });
  client.on("user-unpublished", (user, mediaType) => {
    console.warn("user-unpublished", user.uid, mediaType);
    // 目标流的音频停了，清掉检测结果
    if (String(user.uid) === targetUid.value && mediaType === "audio") {
      resetDetection();
    }
  });
  client.on("user-left", (user) => {
    console.warn("user-left", user.uid);
    // 目标用户离开频道时同样清理检测状态
    if (String(user.uid) === targetUid.value) {
      resetDetection();
    }
  });
}

async function subscribe() {
  if (!client || !joined.value) return;

  const target = client.remoteUsers.find((u) => String(u.uid) === targetUid.value);
  if (!target) {
    notification.warning({
      title: m.value.streamNotFound(targetUid.value),
      duration: 2000,
    });
    return;
  }
  if (!target.hasAudio) {
    notification.warning({
      title: m.value.noAudio(targetUid.value),
      duration: 2000,
    });
    return;
  }

  const track = await client.subscribe(target, "audio");
  remoteAudioTrack = track;
  track.play();
  subscribed.value = true;
  // channelCount 来自信令 pt 值（设计文档中的扩展属性，d.ts 未声明，运行时存在）。
  remoteChannelCount.value = (track as any).channelCount || 0;

  // 每 300ms 取一次各声道音量；数组长度 = 声道数，是判定多声道的主信号。
  window.clearInterval(volumeTimer);
  volumeTimer = window.setInterval(() => {
    if (remoteAudioTrack) volumes.value = remoteAudioTrack.getVolumeLevels();
  }, 300);

  // 底层 codec 检测（opus / multiopus），轮询几次直到拿到结果或超时。
  window.clearInterval(codecTimer);
  let tries = 0;
  codecTimer = window.setInterval(async () => {
    tries++;
    await detectCodec();
    if (codec.value || tries >= 6) window.clearInterval(codecTimer);
  }, 800);
}

// 声道选择：对远端多声道音轨选择任意声道组合，按 surround/stereo/mono 渲染。
// stereo：所选声道里 0,2,4,6… 入左、1,3,5,7… 入右；mono：全部混为单声道（立体声容器）。
function selectChannels(mode: "surround" | "stereo" | "mono") {
  if (!remoteAudioTrack) return;
  const channels = channelsInput.value
    .split(",")
    .map((s) => parseInt(s.trim(), 10))
    .filter((n) => !Number.isNaN(n));
  if (!channels.length) {
    notification.warning({ title: m.value.enterChannels, duration: 2000 });
    return;
  }
  // OPUSMC 当前支持最大 8 声道，合法声道序号为 0~7
  if (channels.some((n) => n < 0 || n > 7)) {
    notification.warning({ title: m.value.channelOutOfRange, duration: 2000 });
    return;
  }
  remoteAudioTrack.selectChannels(channels, mode);
  notification.success({
    title: `selectChannels([${channels.join(",")}], "${mode}")`,
    duration: 2000,
  });
}

async function detectCodec() {
  //@ts-ignore
  const pcs: RTCPeerConnection[] = window.__agoraPcs || [];
  // 从最新的 PC 往回找，避免命中已关闭连接的历史 stats。
  for (let i = pcs.length - 1; i >= 0; i--) {
    try {
      const stats = await pcs[i].getStats();
      let inboundCodecId = "";
      stats.forEach((r: any) => {
        if (r.type === "inbound-rtp" && r.kind === "audio" && r.packetsReceived > 0) {
          inboundCodecId = r.codecId;
        }
      });
      if (!inboundCodecId) continue;
      stats.forEach((r: any) => {
        if (r.type === "codec" && r.id === inboundCodecId && typeof r.mimeType === "string") {
          codec.value = r.mimeType.split("/")[1] || r.mimeType;
          codecChannels.value = Number(r.channels) || 0;
        }
      });
      if (codec.value) return;
    } catch (e) {
      // 忽略单个 pc 的 stats 失败
    }
  }
}

function resetDetection() {
  subscribed.value = false;
  volumes.value = [];
  codec.value = "";
  codecChannels.value = 0;
  remoteChannelCount.value = 0;
  window.clearInterval(volumeTimer);
  window.clearInterval(codecTimer);
  remoteAudioTrack = null;
}

async function leaveChannel() {
  if (published.value) await unpublishAudio();
  resetDetection();
  await client?.leave();
  client = null;
  joined.value = false;
  notification.success({ title: m.value.leftChannel, duration: 1000 });
}

onBeforeUnmount(() => {
  window.clearInterval(volumeTimer);
  window.clearInterval(codecTimer);
  client?.leave();
});
</script>

<style lang="less">
#multiopus {
  padding: 12px;
  button {
    margin: 4px;
  }

  .header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin: 8px;
    padding: 4px 8px;

    .title {
      font-size: 18px;
      font-weight: 600;
    }

    .header-right {
      display: flex;
      align-items: center;
      gap: 12px;

      .docs-link {
        font-size: 14px;
      }
    }
  }

  .wrapper {
    max-width: 800px;
    .input {
      display: flex;

      .input-item {
        margin-right: 8px;
      }
    }
    .links {
      justify-content: space-around;
      a {
        font-size: 14px;
      }
    }
    .operation {
      margin: 8px;
      padding: 8px;
    }

    .content {
      display: flex;
      align-items: center;
      margin: 8px;
      padding: 8px;
      border: 1px solid #ccc;
      border-radius: 4px;
    }

    .result {
      flex-direction: column;
      align-items: stretch;
      gap: 8px;

      &.is-multi {
        border-color: #18a058;
        background-color: #f0faf4;
      }
      &.is-mono {
        border-color: #d03050;
        background-color: #fdf2f4;
      }

      .verdict {
        font-size: 18px;
        .r-label {
          color: #666;
        }
        .r-value {
          font-weight: 600;
        }
      }
      &.is-multi .verdict .r-value {
        color: #18a058;
      }
      &.is-mono .verdict .r-value {
        color: #d03050;
      }

      .meta {
        color: #666;
        font-size: 13px;
      }

      .bars {
        margin-top: 4px;
        .bar-row {
          display: flex;
          align-items: center;
          gap: 8px;
          margin-top: 4px;
          .ch {
            width: 40px;
            font-size: 12px;
            color: #666;
          }
          .outer {
            flex: 1;
            height: 12px;
            background-color: #ddd;
            border-radius: 4px;
            overflow: hidden;
            .inner {
              height: 100%;
              background-color: #2080f0;
              border-radius: 4px;
              transition: width 0.2s;
            }
          }
          .pct {
            width: 44px;
            text-align: right;
            font-size: 12px;
            color: #666;
          }
        }
      }
    }
  }
}
</style>
