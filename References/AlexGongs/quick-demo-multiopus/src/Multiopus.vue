<template>
  <div id="multiopus">
    <div class="wrapper">
      <div class="content input">
        <div class="input-item" style="flex: 1">
          <span>频道 / Channel</span>
          <n-input v-model:value="channel" />
        </div>
        <div class="input-item" style="flex: 1">
          <span>目标 UID (slot) / Target UID (slot)</span>
          <n-input
            v-model:value="targetUid"
            placeholder="要检测的那条流的 uid / uid of the stream to check"
          />
        </div>
      </div>
      <div class="content input">
        <div class="input-item" style="flex: 1">
          <span>本端 UID / Local UID</span>
          <n-input
            v-model:value="myUid"
            placeholder="加入用的 uid（与 token 绑定）/ join uid, bound to token"
          />
        </div>
        <div class="input-item" style="flex: 2">
          <span>Token / 令牌</span>
          <n-input
            v-model:value="token"
            placeholder="本端 uid 对应的 token（无证书项目可留空）/ token for local uid, leave empty if no certificate"
          />
        </div>
      </div>

      <div class="content operation">
        <n-button type="info" @click="joinChannel" :disabled="joined"
          >加入频道 / Join</n-button
        >
        <n-button
          type="success"
          @click="subscribe"
          :disabled="!joined || subscribed"
          >订阅检测 / Subscribe & Check</n-button
        >
        <n-button @click="leaveChannel" :disabled="!joined">离开 / Leave</n-button>
      </div>

      <!-- 检测结果面板 -->
      <div class="content result" :class="resultClass">
        <div class="verdict">
          <span class="r-label">检测结果 / Result：</span>
          <span class="r-value">{{ verdictText }}</span>
        </div>
        <div class="meta">codec：{{ codecText }}</div>
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
  IRemoteAudioTrack,
} from "agora-rtc-sdk-ng";

import { NButton, NInput } from "naive-ui";
import { ref, computed, watch, onBeforeUnmount } from "vue";
import { useNotification } from "naive-ui";

const notification = useNotification();

// 开启多声道（multiopus）解码能力：只有开了，订阅端才可能收到 3-8 声道的多声道流。
// 必须在创建 client 之前设置。
//@ts-ignore
AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true);

const APP_ID = "bbae89fd0cb14412883a3adf1ee57e4a";

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
const channel = ref(
  urlParams.get("channel") || localStorage.getItem("channelName") || "191568709962"
);
const myUid = ref(urlParams.get("uid") || localStorage.getItem("myUid") || "479911");
const token = ref(urlParams.get("token") || localStorage.getItem("token") || "");
const targetUid = ref(
  urlParams.get("subTo") || localStorage.getItem("targetUid") || "336983"
);

watch(channel, (v) => localStorage.setItem("channelName", v));
watch(myUid, (v) => localStorage.setItem("myUid", v));
watch(token, (v) => localStorage.setItem("token", v));
watch(targetUid, (v) => localStorage.setItem("targetUid", v));

let client: IAgoraRTCClient | null = null;
const joined = ref(false);
const subscribed = ref(false);

const volumes = ref<number[]>([]);
let volumeTimer = -1;

const codec = ref(""); // 'opus' | 'multiopus' | ''
const codecChannels = ref(0); // getStats 里 codec 的声道数（opus 在 SDP 里恒为 2，仅参考）
let codecTimer = -1;

let remoteAudioTrack: IRemoteAudioTrack | null = null;

// 主判定信号：getVolumeLevels() 数组长度 = 实际解出的声道数。
const verdictText = computed(() => {
  if (!subscribed.value) return "尚未订阅 / Not subscribed";
  if (volumes.value.length === 0) return "检测中… / Detecting…";
  const n = volumes.value.length;
  const multi = n >= 2 || codec.value === "multiopus";
  return `${n} 声道 / ${n} ch → ${
    multi ? "多声道 / Multi-channel ✓" : "单声道 / Mono ✗"
  }`;
});

const isMulti = computed(
  () => subscribed.value && (volumes.value.length >= 2 || codec.value === "multiopus")
);

const resultClass = computed(() => {
  if (!subscribed.value || volumes.value.length === 0) return "";
  return isMulti.value ? "is-multi" : "is-mono";
});

const codecText = computed(() => {
  if (codec.value === "multiopus")
    return `multiopus（多声道编码 / multi-channel codec${
      codecChannels.value ? "，" + codecChannels.value + " 声道 / ch" : ""
    }）`;
  // opus 的 SDP 声道数恒为 2，不代表内容是立体声，因此不展示该数字以免误读。
  if (codec.value === "opus")
    return "opus（普通编码，非多声道 / standard codec, not multi-channel）";
  if (codec.value) return codec.value;
  return subscribed.value ? "检测中… / Detecting…" : "—";
});

async function joinChannel() {
  if (joined.value) return;
  client = AgoraRTC.createClient({ mode: "rtc", codec: "vp8" });

  await client.join(
    APP_ID,
    channel.value,
    token.value || null,
    Number(myUid.value) || null
  );
  joined.value = true;
  notification.success({ title: "加入成功 / Joined", duration: 1000 });

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
}

async function subscribe() {
  if (!client || !joined.value) return;

  const target = client.remoteUsers.find((u) => String(u.uid) === targetUid.value);
  if (!target) {
    notification.warning({
      title: `频道内未找到 uid ${targetUid.value} 的流 / stream uid ${targetUid.value} not found in channel`,
      duration: 2000,
    });
    return;
  }
  if (!target.hasAudio) {
    notification.warning({
      title: `uid ${targetUid.value} 尚未发布音频 / uid ${targetUid.value} has not published audio`,
      duration: 2000,
    });
    return;
  }

  const track = await client.subscribe(target, "audio");
  remoteAudioTrack = track;
  track.play();
  subscribed.value = true;

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
  window.clearInterval(volumeTimer);
  window.clearInterval(codecTimer);
  remoteAudioTrack = null;
}

async function leaveChannel() {
  resetDetection();
  await client?.leave();
  client = null;
  joined.value = false;
  notification.success({ title: "已离开频道 / Left channel", duration: 1000 });
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

  .wrapper {
    max-width: 800px;
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
