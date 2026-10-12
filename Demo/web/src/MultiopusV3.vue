<template>
  <div id="multiopus-v3">
    <div class="wrapper">
      <!-- 头部：标题 + Live Demo 1/2 链接 + 文档链接 + 中英文切换 -->
      <div class="header">
        <span class="title">{{ m.appTitle }} · Live Demo 3</span>
        <div class="header-right">
          <a class="nav-link" href="?demo=1">Live Demo 1</a>
          <a class="nav-link" href="?demo=2">Live Demo 2</a>
          <a class="nav-link" href="docs/" target="_blank" rel="noopener">{{ m.docs }}</a>
          <n-button size="small" @click="toggleLocale">
            {{ locale === "zh" ? "EN" : "中文" }}
          </n-button>
        </div>
      </div>

      <!-- 行1：频道 / 目标 uid -->
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

      <!-- 行2：本端 uid / App ID -->
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

      <div class="pair-hint">{{ m.pairHint }}</div>

      <!-- 操作按钮：订阅同时拉取对端的视频与多声道音频 -->
      <div class="content operation">
        <n-button type="info" @click="joinChannel" :disabled="joined || joining">{{ m.join }}</n-button>
        <n-button type="success" @click="subscribe" :disabled="!joined || subscribed || subscribing"
          >{{ m.subscribeAv }}</n-button
        >
        <n-button @click="unsubscribe" :disabled="!subscribed">{{ m.unsubscribe }}</n-button>
        <n-button @click="leaveChannel" :disabled="!joined || leaving">{{ m.leave }}</n-button>
        <span class="odd-fix-toggle" :title="m.oddFixHint">
          <span>{{ m.oddFix }}</span>
          <n-switch v-model:value="oddFixEnabled" size="small" />
        </span>
      </div>

      <!-- 声道选择（音频）：先订阅，再选择声道组合与渲染模式 -->
      <div class="content input">
        <div class="input-item" style="flex: 2">
          <span>{{ m.channelsLabel }}</span>
          <n-input v-model:value="channelsInput" :placeholder="m.channelsPh" />
        </div>
        <div class="input-item" style="flex: 3">
          <n-button size="small" @click="selectChannels('surround')" :disabled="!audioSubscribed"
            >surround</n-button
          >
          <n-button size="small" @click="selectChannels('stereo')" :disabled="!audioSubscribed"
            >{{ m.stereoBtn }}</n-button
          >
          <n-button size="small" @click="selectChannels('mono')" :disabled="!audioSubscribed"
            >{{ m.monoBtn }}</n-button
          >
        </div>
      </div>

      <!-- 视频面板：远端视频渲染区 -->
      <div class="content video-panel">
        <div v-show="videoSubscribed" ref="videoEl" class="video-player"></div>
        <span v-if="!videoSubscribed" class="video-empty">{{ videoEmptyText }}</span>
      </div>

      <!-- 音频检测结果面板（与 Live Demo 1 对齐，另加视频 codec） -->
      <div class="content result" :class="resultClass">
        <div class="verdict">
          <span class="r-label">{{ m.result }}:</span>
          <span class="r-value">{{ verdictText }}</span>
        </div>
        <div class="meta">{{ m.audioCodecLabel }}: {{ codecText }}</div>
        <div class="meta" v-if="videoSubscribed">{{ m.videoCodecLabel }}: {{ videoCodecText }}</div>
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
  IAgoraRTCRemoteUser,
  IRemoteAudioTrack,
  IRemoteVideoTrack,
} from "agora-rtc-sdk-ng";

import { NButton, NInput, NSwitch, useNotification } from "naive-ui";
import { oddFixEnabled, patchOddChannelStream, unpatchOddChannelStream } from "./oddChannelFix";
import { ref, computed, watch, onBeforeUnmount } from "vue";

const notification = useNotification();

// ---------------- 中英文切换（与 Live Demo 1/2 同一 localStorage locale 键，互相跟随） ----------------
const locale = ref<"zh" | "en">(
  (localStorage.getItem("locale") as "zh" | "en") ||
    (navigator.language.toLowerCase().startsWith("zh") ? "zh" : "en")
);
function toggleLocale() {
  locale.value = locale.value === "zh" ? "en" : "zh";
}

const messages = {
  zh: {
    appTitle: "Agora 视频 + 多声道音频订阅 Demo",
    docs: "文档",
    channel: "频道",
    targetUid: "目标 UID (subTo)",
    targetUidPh: "要订阅的远端 uid（如 Linux MKV 发送端）",
    myUid: "本端 UID",
    myUidPh: "加入用的 uid（与 token 绑定）",
    appId: "App ID",
    appIdPh: "Agora 项目的 App ID",
    token: "Token",
    tokenPh: "本端 uid 对应的 token（无证书项目可留空）",
    pairHint:
      "配合 Linux MKV demo（sample_send_mkv_multiopus）使用：Linux 端以同一频道发布 1 路视频 + 8 声道音频（4xstereo 布局），在「目标 UID」填入其 uid 后点「订阅音视频」，本页同时渲染远端视频并做逐声道音频检测。也可用 Live Demo 1 在另一标签页发布多声道音频作为源（该源无视频，视频区显示未发布）。订阅期间对端补发视频/音频会自动补订。",
    join: "加入频道",
    leave: "离开",
    subscribeAv: "订阅音视频",
    unsubscribe: "取消订阅",
    channelsLabel: "声道选择（逗号分隔）",
    channelsPh: "例如 0,1 或 0,2,4,6",
    stereoBtn: "stereo（所选声道：偶数序号→左 / 奇数序号→右）",
    monoBtn: "mono（所选声道混为单声道）",
    result: "音频检测结果",
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
    audioCodecLabel: "音频 codec",
    videoCodecLabel: "视频 codec",
    noAudioFromTarget: "已订阅，对端仅发布视频（无音频可检测）",
    noVideoFromTarget: "对端未发布视频",
    joined: "加入成功",
    leftChannel: "已离开频道",
    enterChannels: "请输入声道序号，如 0,1",
    channelOutOfRange: "声道序号必须在 0~7 之间",
    selectionLabel: "当前声道选择",
    streamNotFound: (uid: string) => `频道内未找到 uid ${uid} 的流`,
    noAv: (uid: string) => `uid ${uid} 尚未发布音视频`,
    joinFail: (msg: string) => `加入频道失败: ${msg}`,
    subscribeFail: (msg: string) => `订阅失败: ${msg}`,
    oddFix: "奇数声道补丁",
    oddFixHint: "3/5/7 奇数声道流会被 SDK 误解码（多出一路幻影声道）；开启补丁可修正显示与播放。关闭可对比原始行为",
    oddFixApplied: (trueN: number, decodedN: number) =>
      `检测到奇数声道流：真实 ${trueN} 声道（SDK 误解码为 ${decodedN}），补丁已修正显示与播放`,
    oddFixOff: "已关闭奇数声道补丁，恢复 SDK 原始（误解码）行为",
  },
  en: {
    appTitle: "Agora Video + Multi-Channel Audio Subscribe Demo",
    docs: "Docs",
    channel: "Channel",
    targetUid: "Target UID (subTo)",
    targetUidPh: "uid of the remote to subscribe (e.g. Linux MKV sender)",
    myUid: "Local UID",
    myUidPh: "join uid, bound to token",
    appId: "App ID",
    appIdPh: "App ID of your Agora project",
    token: "Token",
    tokenPh: "token for local uid, leave empty if no certificate",
    pairHint:
      "Pairs with the Linux MKV demo (sample_send_mkv_multiopus): the Linux side publishes 1 video + 8-channel audio (4xstereo layout) into the same channel. Fill its uid into \"Target UID\" and click \"Subscribe A/V\" — this page renders the remote video and runs per-channel audio checks at the same time. You can also publish multi-channel audio from Live Demo 1 in another tab as the source (that source has no video; the video area shows \"not published\"). If the remote side adds video/audio while you are subscribed, it is subscribed automatically.",
    join: "Join",
    leave: "Leave",
    subscribeAv: "Subscribe A/V",
    unsubscribe: "Unsubscribe",
    channelsLabel: "Channels (comma-separated)",
    channelsPh: "e.g. 0,1 or 0,2,4,6",
    stereoBtn: "stereo (selected: even→L / odd→R)",
    monoBtn: "mono (mix selected to mono)",
    result: "Audio result",
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
    audioCodecLabel: "audio codec",
    videoCodecLabel: "video codec",
    noAudioFromTarget: "Subscribed; the remote publishes video only (no audio to check)",
    noVideoFromTarget: "The remote has not published video",
    joined: "Joined",
    leftChannel: "Left channel",
    enterChannels: "Enter channel indices, e.g. 0,1",
    channelOutOfRange: "Channel indices must be between 0 and 7",
    selectionLabel: "Current selection",
    streamNotFound: (uid: string) => `Stream uid ${uid} not found in channel`,
    noAv: (uid: string) => `uid ${uid} has not published audio or video`,
    joinFail: (msg: string) => `Join failed: ${msg}`,
    subscribeFail: (msg: string) => `Subscribe failed: ${msg}`,
    oddFix: "Odd-channel fix",
    oddFixHint: "3/5/7-channel streams are mis-decoded by the SDK (one extra phantom channel). Enable the patch to correct display & playback; disable to compare with the raw behavior",
    oddFixApplied: (trueN: number, decodedN: number) =>
      `Odd-channel stream detected: true ${trueN} ch (SDK mis-decoded as ${decodedN}); display & playback corrected by patch`,
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
    document.title = `${m.value.appTitle} (multiopus-v3)`;
  },
  { immediate: true }
);

// 开启多声道（multiopus）编解码协商能力：只有开了，订阅端才可能收到 4/6/8 声道的
// 多声道流；不开则一律按普通单/双声道处理。
// 注意：必须在 AgoraRTC.createClient() 之前设置，创建 client 后再设置不生效。
//@ts-ignore
AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true);

// 演示用 App ID 默认值（明文 demo 凭证，仅供本地验证；生产环境请在页面内
// 替换为自己的 App ID，并从自己的服务端动态获取 token）。
const DEFAULT_APP_ID = "43f818097c124394821bd44598b7ca9d";

// 钩住 RTCPeerConnection，以便从底层 getStats() 读取实际收到的 codec
// （音频：opus = 普通单/双声道，multiopus = 多声道；视频：h264/vp8 等）。Agora SDK
// 不暴露内部 PC，这里在 client 创建前包一层、收集所有实例，仅用于本检测工具。
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
// URL 参数（channel / uid / token / subTo）优先于 localStorage。
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

// 补丁开关实时切换：订阅中关掉立即撤销（恢复 SDK 原始行为用于对比）；打开则重新检测
watch(oddFixEnabled, (on) => {
  if (!audioSubscribed.value || !remoteAudioTrack) return;
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

// ---------------- 订阅端：同时订阅视频 + 多声道音频 ----------------
let client: IAgoraRTCClient | null = null;
const joined = ref(false);
// subscribed = 已订阅目标用户（至少一路媒体）；audioSubscribed / videoSubscribed 分路标记
const subscribed = ref(false);
const audioSubscribed = ref(false);
const videoSubscribed = ref(false);
// 已订阅目标的 uid：远端 unpublished/left 事件与它比对（而不是实时输入框里的 targetUid）
const subscribedUid = ref("");
// 实际订阅的用户对象：unsubscribe 需要它
let subscribedUser: IAgoraRTCRemoteUser | undefined;
// 异步操作 in-flight 守卫，防止飞行期间重复点击
const joining = ref(false);
const subscribing = ref(false);
const leaving = ref(false);
// 单路媒体订阅 in-flight 守卫：防止手动订阅与 user-published 自动补订并发重复订阅同一路
let audioInflight = false;
let videoInflight = false;

let remoteAudioTrack: IRemoteAudioTrack | null = null;
let remoteVideoTrack: IRemoteVideoTrack | null = null;
const videoEl = ref<HTMLElement | null>(null);
// 补丁检测 generation：teardownAudio（远端 unpublish/leave、取消订阅、离开频道）时递增。
// patchOddChannelStream 的检测窗口最长 8s，期间订阅可能已被拆掉，迟到的 onFixed 与发起时
// 的计数比对，不一致即丢弃，避免把陈旧声道数写回结果面板、补弹过期通知。
let oddFixGeneration = 0;

const volumes = ref<number[]>([]);
let volumeTimer = -1;

// 声道数稳定计数：订阅初期多声道解码管线逐步展开，getVolumeLevels() 会先返回 1/2 路，
// 若立即按数组长度判“单声道”会闪一下红色误判。连续 STABLE_POLLS 次轮询声道数不变才
// 允许出“单声道”结论（出现多声道证据则立即变绿，不受此限制）。
const STABLE_POLLS = 4; // × 300ms 轮询 ≈ 1.2s
const chCountStable = ref(false);
let lastChCount = 0;
let chStableCount = 0;

const codec = ref(""); // 音频 inbound codec：'opus' | 'multiopus' | ''
const codecChannels = ref(0); // getStats 里音频 codec 的声道数（opus 在 SDP 里恒为 2，仅参考）
const videoCodec = ref(""); // 视频 inbound codec：'h264' | 'vp8' | ...
let codecTimer = -1;

const remoteChannelCount = ref(0);

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
  if (!audioSubscribed.value) {
    return subscribed.value ? m.value.noAudioFromTarget : m.value.notSubscribed;
  }
  if (volumes.value.length === 0) return m.value.detecting;
  const n = volumes.value.length;
  const multi = codec.value === "multiopus" || [4, 6, 8].includes(n);
  // 声道数未稳定前不出“单声道”结论，避免订阅初期闪红（见 STABLE_POLLS 注释）
  if (!multi && !chCountStable.value) return m.value.detecting;
  return `${n} ${m.value.chUnit} → ${multi ? m.value.multiChannel : m.value.mono}`;
});

const isMulti = computed(
  () =>
    audioSubscribed.value &&
    (codec.value === "multiopus" || [4, 6, 8].includes(volumes.value.length))
);

const resultClass = computed(() => {
  if (!audioSubscribed.value || volumes.value.length === 0) return "";
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
  return audioSubscribed.value ? m.value.detecting : "—";
});

const videoCodecText = computed(() => videoCodec.value || m.value.detecting);

const videoEmptyText = computed(() =>
  subscribed.value ? m.value.noVideoFromTarget : m.value.notSubscribed
);

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
    // 已订阅用户后来新增发布的媒体（如先订阅音频、视频随后才发布）：自动补订，
    // 保持视频与多声道音频同时在线
    if (subscribed.value && String(user.uid) === subscribedUid.value && mediaType !== "datachannel") {
      void subscribeMedia(user, mediaType);
    }
  });
  client.on("user-unpublished", (user, mediaType) => {
    console.warn("user-unpublished", user.uid, mediaType);
    // 已订阅的那条流停了一路，清掉该路状态（比对已订阅 uid，而非输入框里的 targetUid）
    if (String(user.uid) !== subscribedUid.value) return;
    if (mediaType === "audio") teardownAudio();
    if (mediaType === "video") teardownVideo();
    if (!audioSubscribed.value && !videoSubscribed.value) clearSubscription();
  });
  client.on("user-left", (user) => {
    console.warn("user-left", user.uid);
    // 已订阅用户离开频道时清理全部订阅状态
    if (String(user.uid) === subscribedUid.value) {
      teardownAudio();
      teardownVideo();
      clearSubscription();
    }
  });
}

// 订阅目标用户的单路媒体（audio / video），并接通播放与检测。
// 手动「订阅音视频」与 user-published 自动补订共用此入口。
async function subscribeMedia(user: IAgoraRTCRemoteUser, mediaType: "audio" | "video") {
  if (!client) return;
  if (mediaType === "audio" && (audioSubscribed.value || audioInflight)) return;
  if (mediaType === "video" && (videoSubscribed.value || videoInflight)) return;
  if (mediaType === "audio") audioInflight = true;
  else videoInflight = true;
  try {
    let track: IRemoteAudioTrack | IRemoteVideoTrack;
    try {
      track =
        mediaType === "audio"
          ? await client.subscribe(user, "audio")
          : await client.subscribe(user, "video");
    } catch (e: any) {
      // 竞态（对端恰好 unpublish/left）等导致的订阅失败：提示并保持该路未订阅状态
      notification.error({
        title: m.value.subscribeFail(e?.message || String(e)),
        duration: 3000,
      });
      return;
    }
    // 订阅飞行期间用户已取消订阅/离开：立即退订，避免泄漏（服务端继续推流）
    if (!subscribed.value || String(user.uid) !== subscribedUid.value) {
      try {
        await client?.unsubscribe(user, mediaType);
      } catch {
        /* 忽略清理失败 */
      }
      return;
    }
    if (mediaType === "audio") {
      const audioTrack = track as IRemoteAudioTrack;
      remoteAudioTrack = audioTrack;
      // 调试钩子：暴露到 window，便于控制台/自动化对远端流做逐声道 PCM 测量（如 ChannelSplitter RMS）
      (window as any).__mcRemoteTrack = audioTrack;
      audioTrack.play();
      audioSubscribed.value = true;
      // 奇数声道误解码修正补丁（开关关闭时跳过，用于对比验证；见 oddChannelFix.ts）
      if (oddFixEnabled.value) {
        const oddFixGen = oddFixGeneration;
        void patchOddChannelStream(audioTrack, (r) => {
          // 检测窗口内订阅已被 teardown（远端 unpublish/leave 等）：丢弃迟到回调
          if (oddFixGen !== oddFixGeneration) return;
          notification.warning({ title: m.value.oddFixApplied(r.trueChannels, r.decodedChannels), duration: 5000 });
          remoteChannelCount.value = r.trueChannels;
        });
      }
      // channelCount 来自信令 pt 值（设计文档中的扩展属性，d.ts 未声明，运行时存在）。
      remoteChannelCount.value = (audioTrack as any).channelCount || 0;

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
    } else {
      const videoTrack = track as IRemoteVideoTrack;
      remoteVideoTrack = videoTrack;
      if (videoEl.value) videoTrack.play(videoEl.value);
      videoSubscribed.value = true;
    }
    // 底层 codec 检测（音频 opus/multiopus + 视频 h264/vp8）：800ms 持续轮询——订阅
    // 期间若发生协商回退（multiopus→opus，spec 场景7.1），订阅端可观测到 codec 变化。
    window.clearInterval(codecTimer);
    codecTimer = window.setInterval(() => {
      void detectCodec();
    }, 800);
  } finally {
    if (mediaType === "audio") audioInflight = false;
    else videoInflight = false;
  }
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
  if (!target.hasAudio && !target.hasVideo) {
    notification.warning({
      title: m.value.noAv(targetUid.value),
      duration: 2000,
    });
    return;
  }

  subscribing.value = true;
  // 先登记订阅关系，再逐路订阅：subscribeMedia 的飞行守卫与事件回调都以此为凭
  subscribed.value = true;
  subscribedUid.value = String(target.uid);
  subscribedUser = target;
  if (target.hasAudio) await subscribeMedia(target, "audio");
  if (target.hasVideo) await subscribeMedia(target, "video");
  // 两路都订阅失败时回滚订阅状态
  if (!audioSubscribed.value && !videoSubscribed.value) clearSubscription();
  subscribing.value = false;
}

function clearSubscription() {
  subscribed.value = false;
  subscribedUid.value = "";
  subscribedUser = undefined;
}

async function unsubscribe() {
  if (!client) return;
  const user = subscribedUser;
  // 先清订阅关系，阻止在飞的订阅/自动补订完成后重新挂上
  clearSubscription();
  teardownAudio();
  teardownVideo();
  if (user) {
    try {
      await client.unsubscribe(user, "audio");
    } catch {
      /* 该路本就未订阅时忽略 */
    }
    try {
      await client.unsubscribe(user, "video");
    } catch {
      /* 该路本就未订阅时忽略 */
    }
  }
}

function teardownAudio() {
  audioSubscribed.value = false;
  volumes.value = [];
  codec.value = "";
  codecChannels.value = 0;
  remoteChannelCount.value = 0;
  // 使在飞的 patchOddChannelStream onFixed 回调失效（订阅上下文已 teardown）
  oddFixGeneration++;
  window.clearInterval(volumeTimer);
  remoteAudioTrack = null;
  (window as any).__mcRemoteTrack = null;
  selection.value = null;
  chCountStable.value = false;
  lastChCount = 0;
  chStableCount = 0;
  // codec 轮询为音视频共用：两路都停了才停表
  if (!videoSubscribed.value) window.clearInterval(codecTimer);
}

function teardownVideo() {
  videoSubscribed.value = false;
  videoCodec.value = "";
  remoteVideoTrack?.stop();
  remoteVideoTrack = null;
  if (!audioSubscribed.value) window.clearInterval(codecTimer);
}

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
      let audioCodecId = "";
      let videoCodecId = "";
      stats.forEach((r: any) => {
        if (r.type === "inbound-rtp" && r.packetsReceived > 0) {
          if (r.kind === "audio") audioCodecId = r.codecId;
          if (r.kind === "video") videoCodecId = r.codecId;
        }
      });
      if (!audioCodecId && !videoCodecId) continue;
      stats.forEach((r: any) => {
        if (r.type === "codec" && typeof r.mimeType === "string") {
          const name = r.mimeType.split("/")[1] || r.mimeType;
          if (audioSubscribed.value && r.id === audioCodecId) {
            codec.value = name;
            codecChannels.value = Number(r.channels) || 0;
          }
          if (videoSubscribed.value && r.id === videoCodecId) {
            videoCodec.value = name;
          }
        }
      });
      // 当前已订阅的各路都拿到 codec 后即可返回
      const audioDone = !audioSubscribed.value || !!codec.value;
      const videoDone = !videoSubscribed.value || !!videoCodec.value;
      if (audioDone && videoDone) return;
    } catch (e) {
      // 忽略单个 pc 的 stats 失败
    }
  }
}

async function leaveChannel() {
  if (leaving.value) return;
  leaving.value = true;
  await unsubscribe();
  await client?.leave();
  client = null;
  joined.value = false;
  leaving.value = false;
  notification.success({ title: m.value.leftChannel, duration: 1000 });
}

onBeforeUnmount(() => {
  window.clearInterval(volumeTimer);
  window.clearInterval(codecTimer);
  remoteVideoTrack?.stop();
  client?.leave();
});
</script>

<style lang="less">
#multiopus-v3 {
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

      .nav-link {
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
    .pair-hint {
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
        color: #5a6069;
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

    .video-panel {
      justify-content: center;

      .video-player {
        width: 100%;
        aspect-ratio: 16 / 9;
        background-color: #000;
        border-radius: 4px;
        overflow: hidden;
      }
      .video-empty {
        padding: 24px 0;
        font-size: 13px;
        color: #999;
      }
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
