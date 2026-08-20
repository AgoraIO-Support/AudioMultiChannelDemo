<template>
  <div id="multiopus-v2">
    <div class="wrapper">
      <!-- 头部：标题 + Live Demo 1 链接 + 文档链接 + 中英文切换 -->
      <div class="header">
        <span class="title">{{ m.appTitle }} · Live Demo 2</span>
        <div class="header-right">
          <a class="nav-link" href="?demo=1">Live Demo 1</a>
          <a class="nav-link" href="docs/" target="_blank" rel="noopener">{{ m.docs }}</a>
          <n-button size="small" @click="toggleLocale">
            {{ locale === "zh" ? "EN" : "中文" }}
          </n-button>
        </div>
      </div>

      <!-- 行1：频道 / 多声道音频地址 / 声道数（参考 quick-demo-multiopus 布局） -->
      <div class="content input">
        <div class="input-item">
          <span>{{ m.channel }}</span>
          <n-input v-model:value="channel" />
        </div>
        <div class="input-item" style="width: 60%">
          <span>{{ m.audioUrl }}</span>
          <n-input v-model:value="audioURL" />
        </div>
        <div class="input-item">
          <span>{{ m.channelCount }}</span>
          <n-input-number v-model:value="channelCount" :min="3" :max="8" />
        </div>
      </div>

      <!-- 行2：加入凭证（原参考 demo 写死在代码里，这里放开为输入框） -->
      <div class="content input">
        <div class="input-item">
          <span>{{ m.myUid }}</span>
          <n-input v-model:value="myUid" :placeholder="m.myUidPh" />
        </div>
        <div class="input-item">
          <span>{{ m.appId }}</span>
          <n-input v-model:value="appId" />
        </div>
        <div class="input-item" style="flex: 1">
          <span>{{ m.token }}</span>
          <n-input v-model:value="token" :placeholder="m.tokenPh" />
        </div>
      </div>

      <!-- 行3：声道选择 + 三种渲染模式确认按钮 -->
      <div class="content input">
        <div class="input-item" style="flex: 1">
          <span>{{ m.channelsLabel }}</span>
          <n-input v-model:value="selectedChannels" />
        </div>
        <n-button type="info" @click="selectChannel('surround')" :disabled="!subscribed">
          {{ m.confirmSurround }}
        </n-button>
        <n-button type="info" @click="selectChannel('stereo')" :disabled="!subscribed">
          {{ m.confirmStereo }}
        </n-button>
        <n-button type="info" @click="selectChannel('mono')" :disabled="!subscribed">
          {{ m.confirmMono }}
        </n-button>
      </div>

      <!-- 行4：操作按钮 -->
      <div class="content operation">
        <n-button type="info" @click="joinChannel" :disabled="joined">{{ m.join }}</n-button>
        <n-button type="info" @click="leaveChannel" :disabled="!joined">{{ m.leave }}</n-button>
        <n-button type="info" @click="publishAudio" :disabled="!joined || published">
          {{ m.publish }}
        </n-button>
        <n-button type="info" @click="unpublishAudio" :disabled="!published">
          {{ m.unpublish }}
        </n-button>
        <n-button type="success" @click="subscribe" :disabled="!joined || !hasRemote || subscribed">
          {{ m.subscribe }}
        </n-button>
        <n-button type="info" @click="cancelSubscribe" :disabled="!subscribed">
          {{ m.unsubscribe }}
        </n-button>
      </div>

      <!-- 音量条（参考布局：简洁横条，60% 宽） -->
      <div class="content volumes">
        <div class="outer" v-for="(v, i) in volumes" :key="i">
          <div class="inner" :style="{ width: `${v * 100}%` }"></div>
        </div>
        <span v-if="!volumes.length" class="volumes-empty">{{ m.notSubscribed }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import AgoraRTC from "agora-rtc-sdk-ng";
import type {
  IAgoraRTCClient,
  IAgoraRTCRemoteUser,
  ILocalAudioTrack,
  IRemoteAudioTrack,
} from "agora-rtc-sdk-ng";

import { NButton, NInput, NInputNumber, useNotification } from "naive-ui";
import { ref, computed, watch, onBeforeUnmount } from "vue";

const notification = useNotification();

// ---------------- 中英文切换（与 Live Demo 1 同一 localStorage locale 键，互相跟随） ----------------
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
    channel: "当前频道",
    audioUrl: "多声道音频地址",
    channelCount: "音频声道数",
    myUid: "本端 UID",
    myUidPh: "加入用的 uid（与 token 绑定）",
    appId: "App ID",
    token: "Token",
    tokenPh: "本端 uid 对应的 token（无证书项目可留空）",
    channelsLabel: "选择声道,以逗号(,)隔开(如0,1,2,3,4,5)",
    confirmSurround: "确定(环绕声)",
    confirmStereo: "确定(双声道)",
    confirmMono: "确定(单声道)",
    join: "加入频道",
    leave: "离开频道",
    publish: "发布音频",
    unpublish: "取消发布音频",
    subscribe: "订阅音频",
    unsubscribe: "取消订阅音频",
    notSubscribed: "尚未订阅",
    joined: "加入成功",
    leftChannel: "已离开频道",
    loadingAudio: "正在加载音频文件",
    loadOk: "音频文件加载成功",
    noAudioUrl: "没有输入音频链接",
    noRemote: "频道内暂无可订阅的远端音频",
    enterChannels: "请输入声道序号，如 0,1",
    channelOutOfRange: "声道序号必须在 0~7 之间",
    channelCountInvalid: "声道数必须在 3~8 之间",
    publishedOk: (n: number) => `已发布 ${n} 声道音频`,
    publishFail: (msg: string) => `发布失败: ${msg}`,
  },
  en: {
    appTitle: "Agora Multi-Channel Audio Demo",
    docs: "Docs",
    channel: "Channel",
    audioUrl: "Multi-channel audio URL",
    channelCount: "Channel count",
    myUid: "Local UID",
    myUidPh: "join uid, bound to token",
    appId: "App ID",
    token: "Token",
    tokenPh: "token for local uid, leave empty if no certificate",
    channelsLabel: "Channels, comma-separated (e.g. 0,1,2,3,4,5)",
    confirmSurround: "OK (surround)",
    confirmStereo: "OK (stereo)",
    confirmMono: "OK (mono)",
    join: "Join",
    leave: "Leave",
    publish: "Publish",
    unpublish: "Unpublish",
    subscribe: "Subscribe",
    unsubscribe: "Unsubscribe",
    notSubscribed: "Not subscribed",
    joined: "Joined",
    leftChannel: "Left channel",
    loadingAudio: "Loading audio file",
    loadOk: "Audio file loaded",
    noAudioUrl: "No audio URL entered",
    noRemote: "No remote audio to subscribe in channel",
    enterChannels: "Enter channel indices, e.g. 0,1",
    channelOutOfRange: "Channel indices must be between 0 and 7",
    channelCountInvalid: "Channel count must be between 3 and 8",
    publishedOk: (n: number) => `Published ${n}ch audio`,
    publishFail: (msg: string) => `Publish failed: ${msg}`,
  },
};
const m = computed(() => messages[locale.value]);

// 语言切换联动：持久化选择，并同步 <html lang> 与页面 title。
watch(
  locale,
  (v) => {
    localStorage.setItem("locale", v);
    document.documentElement.lang = v === "zh" ? "zh-CN" : "en";
    document.title = `${m.value.appTitle} (multiopus)`;
  },
  { immediate: true }
);

// 开启多声道（multiopus）编解码协商能力，必须在 createClient 之前设置。
//@ts-ignore
AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true);

// 演示用 App ID 默认值（明文 demo 凭证，正式使用请在页面内替换）。
const DEFAULT_APP_ID = "bbae89fd0cb14412883a3adf1ee57e4a";

const urlParams = new URLSearchParams(location.search);

// 素材地址默认取本站 public/ 下的 8 声道 Channel ID 文件（带 base 前缀，兼容子路径部署）。
const wavBase = import.meta.env.BASE_URL;

const channel = ref(
  urlParams.get("channel") || localStorage.getItem("channelName") || "multiopus"
);
const myUid = ref(urlParams.get("uid") || localStorage.getItem("myUid") || "479911");
const token = ref(urlParams.get("token") || localStorage.getItem("token") || "");
const appId = ref(urlParams.get("appId") || localStorage.getItem("appId") || DEFAULT_APP_ID);
const channelCount = ref<number>(8);
const selectedChannels = ref("0,1,2,3,4,5");
const audioURL = ref(`${wavBase}8_Channel_ID.wav`);

watch(channel, (v) => localStorage.setItem("channelName", v));
watch(myUid, (v) => localStorage.setItem("myUid", v));
watch(token, (v) => localStorage.setItem("token", v));
watch(appId, (v) => localStorage.setItem("appId", v));

// 切换素材时联动声道数（素材文件名即声道数）。
watch(audioURL, (v) => {
  const m2 = v.match(/(\d+)_Channel_ID/);
  if (m2) channelCount.value = Number(m2[1]);
});

// ---------------- 频道与发布/订阅 ----------------
let client: IAgoraRTCClient | null = null;
const joined = ref(false);
const published = ref(false);
const subscribed = ref(false);
const hasRemote = ref(false);
let lastPublishedUser: IAgoraRTCRemoteUser | undefined;

let localAudioTrack: ILocalAudioTrack | null = null;
let remoteAudioTrack: IRemoteAudioTrack | null = null;
let audioContext: AudioContext | null = null;

const volumes = ref<number[]>([]);
let volumeTimer = -1;

async function joinChannel() {
  if (joined.value) return;
  client = AgoraRTC.createClient({ mode: "rtc", codec: "vp8" });

  await client.join(appId.value, channel.value, token.value || null, Number(myUid.value) || null);
  joined.value = true;
  notification.success({ title: m.value.joined, duration: 1000 });

  client.on("user-published", (user, mediaType) => {
    console.warn("user-published", user.uid, mediaType);
    if (mediaType === "audio") {
      lastPublishedUser = user;
      hasRemote.value = true;
    }
  });
  client.on("user-unpublished", (user, mediaType) => {
    console.warn("user-unpublished", user.uid, mediaType);
    if (mediaType === "audio") {
      hasRemote.value = (client?.remoteUsers.some((u) => u.hasAudio)) ?? false;
      if (subscribed.value) cancelSubscribe();
    }
  });
  client.on("user-left", () => {
    hasRemote.value = (client?.remoteUsers.some((u) => u.hasAudio)) ?? false;
  });
}

async function leaveChannel() {
  if (published.value) await unpublishAudio();
  if (subscribed.value) await cancelSubscribe();
  await client?.leave();
  client = null;
  joined.value = false;
  hasRemote.value = false;
  notification.success({ title: m.value.leftChannel, duration: 1000 });
}

// fetch 多声道 WAV 并用 Web Audio 解码为 AudioBuffer。
async function createAudioBuffer(source: string): Promise<AudioBuffer> {
  notification.success({ title: m.value.loadingAudio, duration: 1000 });
  const res = await fetch(source);
  const arrayBuffer = await res.arrayBuffer();
  notification.success({ title: m.value.loadOk, duration: 1000 });
  audioContext = audioContext || new AudioContext();
  return await audioContext.decodeAudioData(arrayBuffer);
}

// AudioBuffer → MediaStreamDestination（显式 channelCount）→ MediaStreamTrack，循环播放。
function getAudioBufferTrack(audioBuffer: AudioBuffer): MediaStreamTrack {
  audioContext = audioContext || new AudioContext();
  const dest = audioContext.createMediaStreamDestination();
  dest.channelCount = audioBuffer.numberOfChannels;
  dest.channelCountMode = "explicit";
  dest.channelInterpretation = "discrete";
  const startSource = () => {
    const source = audioContext!.createBufferSource();
    source.buffer = audioBuffer;
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
  if (!audioURL.value) {
    notification.warning({ title: m.value.noAudioUrl, duration: 1000 });
    return;
  }
  if (channelCount.value < 3 || channelCount.value > 8 || !Number.isFinite(channelCount.value)) {
    notification.warning({ title: m.value.channelCountInvalid, duration: 2000 });
    return;
  }
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
    notification.success({ title: m.value.publishedOk(channelCount.value), duration: 2000 });
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
  await audioContext?.close();
  audioContext = null;
}

async function subscribe() {
  if (!client || !joined.value) return;
  const user =
    lastPublishedUser || client.remoteUsers.find((u) => u.hasAudio) || client.remoteUsers[0];
  if (!user || !user.hasAudio) {
    notification.warning({ title: m.value.noRemote, duration: 2000 });
    return;
  }
  const track = await client.subscribe(user, "audio");
  remoteAudioTrack = track;
  track.play();
  subscribed.value = true;

  window.clearInterval(volumeTimer);
  volumeTimer = window.setInterval(() => {
    if (remoteAudioTrack) volumes.value = remoteAudioTrack.getVolumeLevels();
  }, 300);
}

async function cancelSubscribe() {
  if (!client) return;
  const user = lastPublishedUser || client.remoteUsers[0];
  if (user && remoteAudioTrack) {
    await client.unsubscribe(user, "audio");
  }
  remoteAudioTrack = null;
  subscribed.value = false;
  volumes.value = [];
  window.clearInterval(volumeTimer);
}

// 声道选择：对远端多声道音轨选择任意声道组合，按 surround/stereo/mono 渲染。
function selectChannel(mode: "mono" | "stereo" | "surround") {
  if (!remoteAudioTrack) return;
  const channels = selectedChannels.value
    .split(",")
    .map((v) => parseInt(v.trim(), 10))
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

onBeforeUnmount(() => {
  window.clearInterval(volumeTimer);
  client?.leave();
});
</script>

<style lang="less">
#multiopus-v2 {
  padding: 12px;
  button {
    margin: 4px;
  }

  .wrapper {
    max-width: 800px;

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

    .input {
      display: flex;

      .input-item {
        margin-right: 8px;
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

    .volumes {
      flex-direction: column;
      align-items: stretch;

      .outer {
        margin-top: 6px;
        width: 60%;
        height: 12px;
        background-color: #ccc;
        border-radius: 4px;
        .inner {
          height: 100%;
          background-color: #2080f0;
          border-radius: 4px;
          transition: width 0.2s;
        }
      }
      .volumes-empty {
        font-size: 13px;
        color: #999;
      }
    }
  }
}
</style>
