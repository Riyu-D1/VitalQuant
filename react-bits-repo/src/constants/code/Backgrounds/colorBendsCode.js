import code from '@content/Backgrounds/ColorBends/ColorBends.jsx?raw';
import css from '@content/Backgrounds/ColorBends/ColorBends.css?raw';
import tailwind from '@tailwind/Backgrounds/ColorBends/ColorBends.jsx?raw';
import tsCode from '@ts-default/Backgrounds/ColorBends/ColorBends.tsx?raw';
import tsTailwind from '@ts-tailwind/Backgrounds/ColorBends/ColorBends.tsx?raw';

export const colorBends = {
  dependencies: `three`,
  usage: `import ColorBends from './ColorBends';
  
<ColorBends
  colors={["#ff5c7a", "#8a5cff", "#00ffd1"]}
  rotation={30}
  speed={0.3}
  scale={1.2}
  frequency={1.4}
  warpStrength={1.2}
  mouseInfluence={0.8}
  parallax={0.6}
  noise={0.08}
  transparent
/>`,
  code,
  css,
  tailwind,
  tsCode,
  tsTailwind
};
