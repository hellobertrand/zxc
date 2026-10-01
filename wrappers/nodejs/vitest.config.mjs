// SPDX-License-Identifier: BSD-3-Clause

import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    globals: true,
    testTimeout: 30000,
  },
});
