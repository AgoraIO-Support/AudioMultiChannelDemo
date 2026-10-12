<template>
  <div id="multiopus">
    <div class="wrapper">
      <!-- 头部：标题 + 中英文切换（选择持久化到 localStorage） -->
      <div class="header">
        <span class="title">{{ m.appTitle }} · Live Demo 1</span>
        <div class="header-right">
          <a class="docs-link" href="?demo=2">Live Demo 2</a>
          <a class="docs-link" href="?demo=3">Live Demo 3</a>
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
      <div class="links-hint">{{ m.linksHint }}</div>
      <div class="content links">
        <a :href="publisherLink" target="_blank">{{ m.openPublisher }} (uid: {{ myUid }})</a>
        <a :href="subscriberLink" target="_blank"
          >{{ m.openSubscriber }} (uid: {{ targetUid }} → subTo: {{ myUid }})</a
        >
      </div>

      <div class="content operation">
        <n-button type="info" @click="joinChannel" :disabled="joined || joining">{{ m.join }}</n-button>
        <n-button type="primary" @click="publishAudio" :disabled="!joined || published || publishing"
          >{{ m.publish }}</n-button
        >
        <n-button @click="unpublishAudio" :disabled="!published">{{ m.unpublish }}</n-button>
        <n-button type="success" @click="subscribe" :disabled="!joined || subscribed || subscribing"
          >{{ m.subscribeCheck }}</n-button
        >
        <n-button @click="leaveChannel" :disabled="!joined || leaving">{{ m.leave }}</n-button>
        <span class="odd-fix-toggle" :title="m.oddFixHint">
          <span>{{ m.oddFix }}</span>
          <n-switch v-model:value="oddFixEnabled" size="small" />
        </span>
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
        <div class="selection-banner" v-if="selection" :key="selectionSeq">
          {{ m.selectionLabel }}: {{ selectionText }}
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
        <div class="meter-note" v-if="volumes.length">{{ m.meterNote }}</div>
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

import { NButton, NInput, NSelect, NSwitch } from "naive-ui";
import { oddFixEnabled, patchOddChannelStream, unpatchOddChannelStream } from "./oddChannelFix";
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
    linksHint:
      "双标签页自验证：「打开发布端」开一个新标签页，以本端 UID 加入并发布多声道素材；「打开订阅端」开另一个新标签页，以目标 UID 加入并订阅本端 UID（subTo）的流做逐声道检测。两个链接都会把当前频道名带入新标签页。",
    join: "加入频道",
    publish: "发布音频",
    unpublish: "停止发布",
    subscribeCheck: "订阅检测",
    leave: "离开",
    channelsLabel: "声道选择（逗号分隔）",
    channelsPh: "例如 0,1 或 0,2,4,6",
    stereoBtn: "stereo（所选声道：偶数序号→左 / 奇数序号→右）",
    monoBtn: "mono（所选声道混为单声道）",
    result: "检测结果",
    notSubscribed: "尚未订阅",
    detecting: "检测中…",
    multiChannel: "多声道 ✓",
    mono: "单声道 ✗",
    chUnit: "声道",
    meterNote:
      "注：音量柱为 dB 刻度（仅数字零显示 0%）；静音声道显示 ~10% ≈ -90dB 残余（不可闻），多来自素材静音段的 dither，属正常现象。",
    codecMulti: "多声道编码",
    codecOpus: "普通编码，非多声道",
    remoteCc: "远端音轨 channelCount（信令）",
    joined: "加入成功",
    leftChannel: "已离开频道",
    enterChannels: "请输入声道序号，如 0,1",
    channelOutOfRange: "声道序号必须在 0~7 之间",
    selectionLabel: "当前声道选择",
    wavLabel: (n: number) => `${n}_Channel_ID.wav（${n} 声道）`,
    publishedOk: (n: number) => `已发布 ${n} 声道音频`,
    publishFail: (msg: string) => `发布失败: ${msg}`,
    streamNotFound: (uid: string) => `频道内未找到 uid ${uid} 的流`,
    noAudio: (uid: string) => `uid ${uid} 尚未发布音频`,
    joinFail: (msg: string) => `加入频道失败: ${msg}`,
    subscribeFail: (msg: string) => `订阅失败: ${msg}`,
    oddFixApplied: (trueN: number, decodedN: number) =>
      `检测到奇数声道流：真实 ${trueN} 声道（SDK 误解码为 ${decodedN}），补丁已修正显示与播放`,
    oddFix: "奇数声道补丁",
    oddFixHint: "3/5/7 奇数声道流会被 SDK 误解码（多出一路幻影声道）；开启补丁可修正显示与播放。关闭可对比原始行为",
    oddFixOff: "已关闭奇数声道补丁，恢复 SDK 原始（误解码）行为",
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
    linksHint:
      "Dual-tab self-verification: \"Open Publisher\" opens a new tab that joins as Local UID and publishes the multi-channel WAV; \"Open Subscriber\" opens another new tab that joins as Target UID and subscribes to Local UID's stream (subTo) for per-channel checking. Both links carry the current channel name into the new tab.",
    join: "Join",
    publish: "Publish",
    unpublish: "Unpublish",
    subscribeCheck: "Subscribe & Check",
    leave: "Leave",
    channelsLabel: "Channels (comma-separated)",
    channelsPh: "e.g. 0,1 or 0,2,4,6",
    stereoBtn: "stereo (selected: even→L / odd→R)",
    monoBtn: "mono (mix selected to mono)",
    result: "Result",
    notSubscribed: "Not subscribed",
    detecting: "Detecting…",
    multiChannel: "Multi-channel ✓",
    mono: "Mono ✗",
    chUnit: "ch",
    meterNote:
      "Note: the bars use a dB scale (0% = digital zero only). A silent channel reading ~10% means an inaudible ~-90 dB residue, usually dither from the source material — this is normal.",
    codecMulti: "multi-channel codec",
    codecOpus: "standard codec, not multi-channel",
    remoteCc: "Remote track channelCount (signaling)",
    joined: "Joined",
    leftChannel: "Left channel",
    enterChannels: "Enter channel indices, e.g. 0,1",
    channelOutOfRange: "Channel indices must be between 0 and 7",
    selectionLabel: "Current selection",
    wavLabel: (n: number) => `${n}_Channel_ID.wav (${n}ch)`,
    publishedOk: (n: number) => `Published ${n}ch audio`,
    publishFail: (msg: string) => `Publish failed: ${msg}`,
    streamNotFound: (uid: string) => `Stream uid ${uid} not found in channel`,
    noAudio: (uid: string) => `uid ${uid} has not published audio`,
    joinFail: (msg: string) => `Join failed: ${msg}`,
    subscribeFail: (msg: string) => `Subscribe failed: ${msg}`,
    oddFixApplied: (trueN: number, decodedN: number) =>
      `Odd-channel stream detected: true ${trueN} ch (SDK mis-decoded as ${decodedN}); display & playback corrected by patch`,
    oddFix: "Odd-channel fix",
    oddFixHint: "3/5/7-channel streams are mis-decoded by the SDK (one extra phantom channel). Enable the patch to correct display & playback; disable to compare with the raw behavior",
    oddFixOff: "Odd-channel fix off — back to the raw (mis-decoded) SDK behavior",
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
const DEFAULT_APP_ID = "43f818097c124394821bd44598b7ca9d";

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
  if (!client || !joined.value || published.value || publishing.value) return;
  publishing.value = true;
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
    // 发布失败时关闭残留 track 与 AudioContext，避免泄漏和循环播放残留。
    localAudioTrack?.close();
    localAudioTrack = null;
    await audioContext?.close();
    audioContext = null;
  } finally {
    publishing.value = false;
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
// 已订阅目标的 uid：远端 unpublished/left 事件与它比对（而不是实时输入框里的 targetUid）
const subscribedUid = ref("");
// 异步操作 in-flight 守卫，防止飞行期间重复点击
const joining = ref(false);
const publishing = ref(false);
const subscribing = ref(false);
const leaving = ref(false);

const volumes = ref<number[]>([]);
let volumeTimer = -1;

// 声道数稳定计数：订阅初期多声道解码管线逐步展开，getVolumeLevels() 会先返回 1/2 路，
// 若立即按数组长度判“单声道”会闪一下红色误判。连续 STABLE_POLLS 次轮询声道数不变才
// 允许出“单声道”结论（出现多声道证据则立即变绿，不受此限制）。
const STABLE_POLLS = 4; // × 300ms 轮询 ≈ 1.2s
const chCountStable = ref(false);
let lastChCount = 0;
let chStableCount = 0;

const codec = ref(""); // 'opus' | 'multiopus' | ''
const codecChannels = ref(0); // getStats 里 codec 的声道数（opus 在 SDP 里恒为 2，仅参考）
let codecTimer = -1;

let remoteAudioTrack: IRemoteAudioTrack | null = null;
const remoteChannelCount = ref(0);
// 补丁检测 generation：resetDetection（远端 unpublish/leave、离开频道等 teardown）时递增。
// patchOddChannelStream 的检测窗口最长 8s，期间订阅可能已被拆掉，迟到的 onFixed 与发起时
// 的计数比对，不一致即丢弃，避免把陈旧声道数写回结果面板、补弹过期通知。
let oddFixGeneration = 0;

const channelsInput = ref("0,1");

// 当前生效的声道选择：selectChannels 调用成功后记录，显示在结果面板
const selection = ref<{ channels: number[]; mode: string } | null>(null);
// 每次成功选择 +1：作为 :key 重建提示条以重播闪动动画，让用户明显感知变化
const selectionSeq = ref(0);
const selectionText = computed(() => {
  if (!selection.value) return "";
  return `[${selection.value.channels.join(",")}] · ${selection.value.mode}`;
});

// 主判定信号：getVolumeLevels() 数组长度 = 实际解出的声道数。
const verdictText = computed(() => {
  if (!subscribed.value) return m.value.notSubscribed;
  if (volumes.value.length === 0) return m.value.detecting;
  const n = volumes.value.length;
  const multi = codec.value === "multiopus" || [4, 6, 8].includes(n);
  // 声道数未稳定前不出“单声道”结论，避免订阅初期闪红（见 STABLE_POLLS 注释）
  if (!multi && !chCountStable.value) return m.value.detecting;
  return `${n} ${m.value.chUnit} → ${multi ? m.value.multiChannel : m.value.mono}`;
});

const isMulti = computed(
  () => subscribed.value && (codec.value === "multiopus" || [4, 6, 8].includes(volumes.value.length))
);

const resultClass = computed(() => {
  if (!subscribed.value || volumes.value.length === 0) return "";
  if (isMulti.value) return "is-multi";
  // 声道数未稳定前保持中性配色，避免闪红
  return chCountStable.value ? "is-mono" : "";
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
  if (joined.value || joining.value) return;
  joining.value = true;
  client = AgoraRTC.createClient({ mode: "rtc", codec: "vp8" });
  try {
    const assignedUid = await client.join(
      appId.value,
      channel.value,
      token.value || null,
      Number(myUid.value) || null
    );
    // 未填 uid（或服务端分配）时回显实际 uid，方便订阅端填写 subTo
    if (!Number(myUid.value)) myUid.value = String(assignedUid);
  } catch (e: any) {
    // 加入失败（token 错误/网络失败等）：提示并清理 client，避免半连接泄漏且无法 Leave
    notification.error({ title: m.value.joinFail(e?.message || String(e)), duration: 3000 });
    try {
      await client.leave();
    } catch {
      /* 忽略清理失败 */
    }
    client = null;
    joining.value = false;
    return;
  }
  joining.value = false;
  joined.value = true;
  notification.success({ title: m.value.joined, duration: 1000 });

  client.on("user-published", (user, mediaType) => {
    console.warn("user-published", user.uid, mediaType);
  });
  client.on("user-unpublished", (user, mediaType) => {
    console.warn("user-unpublished", user.uid, mediaType);
    // 已订阅的那条流的音频停了，清掉检测结果（比对已订阅 uid，而非输入框里的 targetUid）
    if (String(user.uid) === subscribedUid.value && mediaType === "audio") {
      resetDetection();
      subscribedUid.value = "";
    }
  });
  client.on("user-left", (user) => {
    console.warn("user-left", user.uid);
    // 已订阅用户离开频道时同样清理检测状态
    if (String(user.uid) === subscribedUid.value) {
      resetDetection();
      subscribedUid.value = "";
    }
  });
}

async function subscribe() {
  if (!client || !joined.value || subscribed.value || subscribing.value) return;

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

  subscribing.value = true;
  let track: IRemoteAudioTrack;
  try {
    track = await client.subscribe(target, "audio");
  } catch (e: any) {
    // 竞态（对端恰好 unpublish/left）等导致的订阅失败：提示并保持未订阅状态
    notification.error({
      title: m.value.subscribeFail(e?.message || String(e)),
      duration: 3000,
    });
    subscribing.value = false;
    return;
  }
  subscribing.value = false;
  remoteAudioTrack = track;
  // 调试钩子：暴露到 window，便于控制台/自动化对远端流做逐声道 PCM 测量（如 ChannelSplitter RMS）
  (window as any).__mcRemoteTrack = track;
  track.play();
  subscribed.value = true;
  subscribedUid.value = String(target.uid);
  // channelCount 来自信令 pt 值（设计文档中的扩展属性，d.ts 未声明，运行时存在）。
  remoteChannelCount.value = (track as any).channelCount || 0;

  // 奇数声道流修正补丁（不改 SDK 文件，见 src/oddChannelFix.ts 与
  // Test/odd_channels_interop_test_20261012.md §5/§6）：检测「末两声道恒等」的幻影签名，
  // 命中后修正音量显示声道数、channelCount 与播放输出；开关关闭时跳过（用于对比验证）
  if (oddFixEnabled.value) {
    const oddFixGen = oddFixGeneration;
    void patchOddChannelStream(track, (r) => {
      // 检测窗口内订阅已被 teardown（远端 unpublish/leave 等）：丢弃迟到回调
      if (oddFixGen !== oddFixGeneration) return;
      notification.warning({
        title: m.value.oddFixApplied(r.trueChannels, r.decodedChannels),
        duration: 5000,
      });
      remoteChannelCount.value = r.trueChannels;
    });
  }

  // 每 300ms 取一次各声道音量；数组长度 = 声道数，是判定多声道的主信号。
  // channelCount 在 subscribe resolve 时可能尚未就绪，随音量轮询一并刷新。
  window.clearInterval(volumeTimer);
  volumeTimer = window.setInterval(() => {
    if (remoteAudioTrack) {
      volumes.value = remoteAudioTrack.getVolumeLevels();
      remoteChannelCount.value = (remoteAudioTrack as any).channelCount || 0;
      const n = volumes.value.length;
      if (n === lastChCount) chStableCount++;
      else {
        lastChCount = n;
        chStableCount = 1;
      }
      chCountStable.value = chStableCount >= STABLE_POLLS;
    }
  }, 300);

  // 底层 codec 检测（opus / multiopus）：800ms 持续轮询——订阅期间若发生协商
  // 回退（multiopus→opus，spec 场景7.1），订阅端可观测到 codec 变化。
  window.clearInterval(codecTimer);
  codecTimer = window.setInterval(() => {
    void detectCodec();
  }, 800);
}

// 补丁开关实时切换：订阅中关掉 → 立即撤销补丁（恢复 SDK 原始行为用于对比）；
// 订阅中打开 → 重新跑检测，命中即修正。未订阅时只影响下一次订阅。
watch(oddFixEnabled, (on) => {
  if (!subscribed.value || !remoteAudioTrack) return;
  if (!on) {
    if (unpatchOddChannelStream(remoteAudioTrack)) {
      remoteChannelCount.value = (remoteAudioTrack as any).channelCount || 0;
      notification.info({ title: m.value.oddFixOff, duration: 3000 });
    }
  } else if (!(remoteAudioTrack as any).__oddFix) {
    const oddFixGen = oddFixGeneration;
    void patchOddChannelStream(remoteAudioTrack, (r) => {
      // 检测窗口内订阅已被 teardown（远端 unpublish/leave 等）：丢弃迟到回调
      if (oddFixGen !== oddFixGeneration) return;
      notification.warning({
        title: m.value.oddFixApplied(r.trueChannels, r.decodedChannels),
        duration: 5000,
      });
      remoteChannelCount.value = r.trueChannels;
    });
  }
});

// 声道选择：对远端多声道音轨选择任意声道组合，按 surround/stereo/mono 渲染。
// stereo：所选声道里 0,2,4,6… 入左、1,3,5,7… 入右；mono：全部混为单声道（立体声容器）。
function selectChannels(mode: "surround" | "stereo" | "mono") {
  if (!remoteAudioTrack) return;
  // 严格解析：只接受纯数字序号并去重（"0,0,1"、"1abc"、"0.9" 不再被静默接受）
  const channels = [
    ...new Set(
      channelsInput.value
        .split(",")
        .map((s) => s.trim())
        .filter((s) => /^\d+$/.test(s))
        .map(Number)
    ),
  ];
  if (!channels.length) {
    notification.warning({ title: m.value.enterChannels, duration: 2000 });
    return;
  }
  // OPUSMC 当前支持最大 8 声道，合法声道序号为 0~7
  if (channels.some((n) => n < 0 || n > 7)) {
    notification.warning({ title: m.value.channelOutOfRange, duration: 2000 });
    return;
  }
  try {
    remoteAudioTrack.selectChannels(channels, mode);
  } catch (e: any) {
    notification.error({ title: `selectChannels failed: ${e?.message || String(e)}`, duration: 3000 });
    return;
  }
  // API 调用成功：记录当前生效的声道选择，显示在结果面板
  selection.value = { channels, mode };
  selectionSeq.value++;
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
  // 使在飞的 patchOddChannelStream onFixed 回调失效（订阅上下文已 teardown）
  oddFixGeneration++;
  window.clearInterval(volumeTimer);
  window.clearInterval(codecTimer);
  remoteAudioTrack = null;
  (window as any).__mcRemoteTrack = null;
  selection.value = null;
  chCountStable.value = false;
  lastChCount = 0;
  chStableCount = 0;
}

async function leaveChannel() {
  if (leaving.value) return;
  leaving.value = true;
  if (published.value) await unpublishAudio();
  resetDetection();
  subscribedUid.value = "";
  await client?.leave();
  client = null;
  joined.value = false;
  leaving.value = false;
  notification.success({ title: m.value.leftChannel, duration: 1000 });
}

onBeforeUnmount(() => {
  window.clearInterval(volumeTimer);
  window.clearInterval(codecTimer);
  // 关闭本地 track 与 AudioContext，避免 HMR 时循环播放的 source 成为孤儿（幽灵音频）
  localAudioTrack?.close();
  void audioContext?.close();
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
    .links-hint {
      margin: 8px;
      padding: 0 8px;
      font-size: 13px;
      color: #888;
    }
    .operation {
      margin: 8px;
      padding: 8px;
      .odd-fix-toggle {
        margin-left: auto;
        display: inline-flex;
        align-items: center;
        gap: 6px;
        font-size: 13px;
        color: var(--text-secondary, #5a6069);
      }
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

      // 声道选择提示条：高亮常驻 + 每次选择后闪动一次（动画见底部 keyframes）
      .selection-banner {
        padding: 6px 10px;
        border: 1px solid #2080f0;
        border-radius: 4px;
        background-color: #e8f3ff;
        color: #2080f0;
        font-size: 14px;
        font-weight: 600;
        animation: selection-flash 1.2s ease-out;
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

      .meter-note {
        margin-top: 6px;
        font-size: 12px;
        color: #999;
      }
    }
  }
}

// 声道选择生效时的闪动动画：蓝底白字亮起，渐变为常驻浅蓝提示条
@keyframes selection-flash {
  0% {
    background-color: #2080f0;
    color: #fff;
    box-shadow: 0 0 0 4px rgba(32, 128, 240, 0.35);
  }
  100% {
    background-color: #e8f3ff;
    color: #2080f0;
    box-shadow: none;
  }
}
</style>
