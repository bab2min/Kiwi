import { defineConfig } from 'vite';
import { viteStaticCopy } from 'vite-plugin-static-copy';
import { requiredModelFiles } from './src/modelFiles';

const targets = requiredModelFiles.map((file) => ({
    src: '../../../models/base/' + file,
    dest: 'model',
}));

export default defineConfig({
    plugins: [
        viteStaticCopy({ targets }),
    ],
});
