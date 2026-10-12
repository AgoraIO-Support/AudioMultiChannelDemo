// 奇数声道（3/5/7）multiopus 流在 Web SDK 侧的误码修正补丁。
//
// 背景（2026-10-12 实测与源码阅读，详见 Test/odd_channels_interop_test_20261012.md §5）：
// Agora Web SDK（agora-rtc-sdk-ng 4.20.1-200）的 multiopus 声道表写死为偶数（4+2*t → 4/6/8），
// SDP fmtp 与解码后拆声道数量都查该表。收到 3/5/7 奇数声道流时，浏览器按错误的 N+1 偶数声道
// 配置解码：stereo 子流正常，最后的 mono 子流被当 stereo 解成左右复制——解码输出的末两个声道
// 逐样本恒等（幻影声道），内容本身不丢。
//
// 本补丁不改 SDK 文件：订阅拿到远端音轨后调用 patchOddChannelStream(track)：
// 1. 检测幻影签名——transformer.lastSplitAudioData 末两声道 PCM 逐样本一致；
// 2. 命中后：getVolumeLevels 去掉幻影项（显示真实 N 声道）、track.channelCount 改为真实 N、
//    selectChannels([0..N-1], surround) 让播放输出丢掉幻影声道（避免末声道内容重复响度）。
//
// 注意：检测依赖「末两声道恒等」签名，理论上可能与「末两声道恰好同内容的真偶数流」撞车；
// 对 Channel ID 系列逐声道可辨素材不会误判。补丁只对当次订阅生效，unpublish 后随 track 销毁。

import type { IRemoteAudioTrack } from "agora-rtc-sdk-ng";
import { ref, watch } from "vue";

// ---------------- 补丁开关（三个 Live Demo 共用，localStorage 持久化） ----------------
// 默认开启；关闭后恢复 SDK 原始（误）行为，便于集成方做对比验证。
export const oddFixEnabled = ref(localStorage.getItem("oddFixEnabled") !== "0");
watch(oddFixEnabled, (v) => localStorage.setItem("oddFixEnabled", v ? "1" : "0"));

export interface OddFixResult {
  /** 是否检测到并修正了奇数声道流 */
  fixed: boolean;
  /** 真实声道数（命中时） */
  trueChannels: number;
  /** 解码输出的声道数（N+1，命中时） */
  decodedChannels: number;
}

interface TrackInternals {
  transformer?: {
    channelCount: number;
    lastSplitAudioData: Float32Array[];
    selectChannels: (channels: number[], mode: string) => void;
  };
  channelCount?: number;
}

/** 两个 Float32 平面是否逐样本一致（允许 1e-6 误差） */
function planesIdentical(a: Float32Array, b: Float32Array): boolean {
  if (a.length !== b.length || a.length === 0) return false;
  for (let i = 0; i < a.length; i += 8) {
    if (Math.abs(a[i] - b[i]) > 1e-6) return false;
  }
  return true;
}

/**
 * 采样检测并在命中时修正。返回 Promise，在给出结论时 resolve（无论是否命中）。
 * @param track  远端音轨（subscribe 返回值）
 * @param onFixed 命中并修正后的回调（用于 UI 提示）
 * @param maxWaitMs 最长检测窗口（默认 8 s：需等 codec 统计到达、transformer 建好）
 */
export function patchOddChannelStream(
  track: IRemoteAudioTrack,
  onFixed?: (r: OddFixResult) => void,
  maxWaitMs = 8000,
): Promise<OddFixResult> {
  return new Promise((resolve) => {
    const t0 = Date.now();
    let frames = 0;
    let identFrames = 0;
    let sawNonZero = false;

    const timer = window.setInterval(() => {
      const tr = (track as unknown as TrackInternals).transformer;
      const planes = tr?.lastSplitAudioData;
      if (tr && planes && planes.length >= 4 && planes.length % 2 === 0) {
        const n = planes.length;
        const a = planes[n - 2];
        const b = planes[n - 1];
        if (a && b && a.length > 0) {
          frames++;
          if (planesIdentical(a, b)) {
            identFrames++;
            if (!sawNonZero) {
              for (let i = 0; i < a.length; i += 16) {
                if (a[i] !== 0) { sawNonZero = true; break; }
              }
            }
          }
        }
      }

      // 判定：≥10 帧且全部恒等且见过非零内容 → 末位为幻影，真实声道数 = n-1
      const decided = frames >= 10 && identFrames === frames && sawNonZero;
      const timeout = Date.now() - t0 > maxWaitMs;
      if (!decided && !timeout) return;

      window.clearInterval(timer);
      if (!decided || !tr) {
        resolve({ fixed: false, trueChannels: tr?.channelCount ?? 0, decodedChannels: tr?.channelCount ?? 0 });
        return;
      }

      const decodedChannels = tr.channelCount;
      const trueChannels = decodedChannels - 1;

      // ① 显示修正：去掉幻影末声道（原函数留在 track.__oddFix 供撤销）
      const origGetVolumeLevels = track.getVolumeLevels.bind(track);
      (track as unknown as { getVolumeLevels: (channels?: number[]) => number[] }).getVolumeLevels =
        (channels?: number[]) => {
          const v = origGetVolumeLevels(channels);
          return !channels && v.length === decodedChannels ? v.slice(0, trueChannels) : v;
        };
      // ② channelCount 修正为真实声道数
      (track as unknown as TrackInternals).channelCount = trueChannels;
      // ③ 播放修正：surround 只选真实声道，幻影声道输出静音
      try {
        track.selectChannels(Array.from({ length: trueChannels }, (_, i) => i), "surround");
      } catch {
        /* 老链路无 transformer 时忽略 */
      }

      // 记录撤销所需的原始状态（供开关关闭时恢复 SDK 原始行为）
      (track as unknown as { __oddFix?: unknown }).__oddFix = {
        origGetVolumeLevels,
        decodedChannels,
      };

      const r = { fixed: true, trueChannels, decodedChannels };
      onFixed?.(r);
      resolve(r);
    }, 100);
  });
}

/** 撤销补丁（开关关闭时调用）：恢复 SDK 原始（误）行为，便于对比验证。返回是否确有撤销。 */
export function unpatchOddChannelStream(track: IRemoteAudioTrack): boolean {
  const st = (track as unknown as { __oddFix?: {
    origGetVolumeLevels: (channels?: number[]) => number[];
    decodedChannels: number;
  } }).__oddFix;
  if (!st) return false;
  track.getVolumeLevels = st.origGetVolumeLevels;
  (track as unknown as TrackInternals).channelCount = st.decodedChannels;
  try {
    // 恢复播放全部解码声道（含幻影末声道，即 SDK 原始行为）
    track.selectChannels(Array.from({ length: st.decodedChannels }, (_, i) => i), "surround");
  } catch {
    /* 忽略 */
  }
  delete (track as unknown as { __oddFix?: unknown }).__oddFix;
  return true;
}
