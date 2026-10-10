<template>
  <n-notification-provider>
    <MultiopusV3 v-if="demo === '3'" />
    <MultiopusV2 v-else-if="demo === '2'" />
    <Multiopus v-else />
  </n-notification-provider>
</template>

<script setup lang="ts">
import { NNotificationProvider } from "naive-ui";
import Multiopus from "./Multiopus.vue";
import MultiopusV2 from "./MultiopusV2.vue";
import MultiopusV3 from "./MultiopusV3.vue";

// Live Demo 切换：?demo=2 为参考布局（quick-demo-multiopus 风格），?demo=3 为视频+多声道音频同时订阅，默认原版布局
const demo = new URLSearchParams(location.search).get("demo");
</script>
