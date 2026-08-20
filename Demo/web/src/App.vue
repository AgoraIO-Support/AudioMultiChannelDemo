<template>
  <n-notification-provider>
    <MultiopusV2 v-if="isV2" />
    <Multiopus v-else />
  </n-notification-provider>
</template>

<script setup lang="ts">
import { NNotificationProvider } from "naive-ui";
import Multiopus from "./Multiopus.vue";
import MultiopusV2 from "./MultiopusV2.vue";

// Live Demo 切换：?demo=2 为参考布局（quick-demo-multiopus 风格），默认原版布局
const isV2 = new URLSearchParams(location.search).get("demo") === "2";
</script>
