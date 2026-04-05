import { useRef, useEffect, useState } from 'react';
import { Renderer, Camera, Transform, Program, Mesh, Geometry } from 'ogl';

const vertex = /* glsl */ `
attribute vec2 position;
void main(){
  gl_Position=vec4(position,0.,1.);
}
`;

const fragment = /* glsl */ `
precision mediump float;
uniform float iTime;
uniform vec2  iResolution;
uniform vec2  uOffset;
uniform float uRotation;
uniform float focalLength;
uniform float speed1;
uniform float speed2;
uniform float dir2;
uniform float bend1;
uniform float bend2;
uniform float bendAdj1;
uniform float bendAdj2;
uniform float uOpacity;

const float lt   = 0.3;
const float pi   = 3.14159;
const float pi2  = 6.28318;
const float pi_2 = 1.5708;
#define MAX_STEPS 14

void mainImage(out vec4 C, in vec2 U) {
  float t = iTime * pi;
  float s = 1.0;
  float d = 0.0;
  vec2  R = iResolution;

  vec3 o = vec3(0.0, 0.0, -7.0);
  vec3 u = normalize(vec3((U - 0.5 * R) / R.y, focalLength));
  vec2 k = vec2(0.0);
  vec3 p;

  float t1 = t * 0.7;
  float t2 = t * 0.9;
  float tSpeed1 = t * speed1;
  float tSpeed2 = t * speed2 * dir2;
  float wob1Base = bend1 + bendAdj1;
  float wob2Base = bend2 + bendAdj2;

  for (int i = 0; i < MAX_STEPS; ++i) {
    p = o + u * d;
    p.x -= 15.0;

    float px = p.x;
    float wob1 = wob1Base + sin(t1 + px * 0.8) * 0.1;
    float wob2 = wob2Base + cos(t2 + px * 1.1) * 0.1;

    float px2 = px + pi_2;
    vec2 sinOffset = sin(vec2(px, px2) + tSpeed1) * wob1;
    vec2 cosOffset = cos(vec2(px, px2) + tSpeed2) * wob2;

    vec2 yz = p.yz;
    float pxLt = px + lt;
    k.x = max(pxLt, length(yz - sinOffset) - lt);
    k.y = max(pxLt, length(yz - cosOffset) - lt);

    float current = min(k.x, k.y);
    s = min(s, current);
    if (s < 0.001 || d > 300.0) break;
    d += s * 0.7;
  }

  float sqrtD = sqrt(d);
  vec3 c = max(cos(d * pi2) - s * sqrtD - vec3(k, 0.0), 0.0);
  c.gb += 0.1;
  float maxC = max(c.r, max(c.g, c.b));
  if (maxC < 0.15) discard;
  c = c * 0.4 + c.brg * 0.6 + c * c;
  C = vec4(c, uOpacity);
}

void main() {
  vec2 coord = gl_FragCoord.xy + uOffset;
  coord -= 0.5 * iResolution;
  float c = cos(uRotation), s = sin(uRotation);
  coord = mat2(c, -s, s, c) * coord;
  coord += 0.5 * iResolution;

  vec4 color;
  mainImage(color, coord);
  gl_FragColor = color;
}
`;

export default function PlasmaWaveV2({
  xOffset = 0,
  yOffset = 0,
  rotationDeg = 0,
  focalLength = 0.8,
  speed1 = 0.05,
  speed2 = 0.05,
  dir2 = 1.0,
  bend1 = 1,
  bend2 = 0.5,
  fadeInDuration = 2000,
  pauseWhenOffscreen = true,
  rootMargin = '200px',
  autoPauseOnScroll = true,
  scrollPauseThreshold = null,
  resumeOnScrollUp = false,
  dynamicDpr = false
}) {
  const [fallbackActive, setFallbackActive] = useState(false);
  const [isMobile, setIsMobile] = useState(false);
  const containerRef = useRef(null);
  const uniformOffset = useRef(new Float32Array([xOffset, yOffset]));
  const uniformResolution = useRef(new Float32Array([1, 1]));
  const rendererRef = useRef(null);
  const fadeStartTime = useRef(null);
  const startTimeRef = useRef(0);
  const resizeTimeoutRef = useRef(null);
  const rafRef = useRef(null);
  const runningRef = useRef(false);
  const observerRef = useRef(null);
  const permaPausedRef = useRef(false);
  const startStopApiRef = useRef({ start: () => {}, stop: () => {} });
  const appliedScrollThresholdRef = useRef(null);

  const propsRef = useRef({});
  propsRef.current = { xOffset, yOffset, rotationDeg, focalLength, speed1, speed2, dir2, bend1, bend2, fadeInDuration };

  useEffect(() => {
    const f = () => setIsMobile(window.innerWidth <= 768);
    f();
    window.addEventListener('resize', f);
    return () => window.removeEventListener('resize', f);
  }, []);

  useEffect(() => {
    if (isMobile || fallbackActive) return;

    const createRenderer = () => {
      const commonOpts = {
        alpha: true,
        dpr: 0.5,
        antialias: false,
        depth: false,
        stencil: false,
        premultipliedAlpha: false,
        preserveDrawingBuffer: false,
        powerPreference: 'high-performance',
        failIfMajorPerformanceCaveat: false
      };

      try {
        const r = new Renderer(commonOpts);
        return r;
      } catch (err) {
        try {
          const canvas = document.createElement('canvas');
          const attrs = {
            alpha: true,
            antialias: false,
            depth: false,
            stencil: false,
            premultipliedAlpha: false,
            preserveDrawingBuffer: false,
            powerPreference: 'high-performance',
            failIfMajorPerformanceCaveat: false
          };
          const gl =
            canvas.getContext('webgl2', attrs) ||
            canvas.getContext('webgl', attrs) ||
            canvas.getContext('experimental-webgl', attrs);
          if (!gl) return null;
          const r = new Renderer({ ...commonOpts, gl });
          return r;
        } catch (e2) {
          return null;
        }
      }
    };

    const renderer = createRenderer();
    if (!renderer) {
      setFallbackActive(true);
      return;
    }
    rendererRef.current = renderer;

    const gl = renderer.gl;
    gl.clearColor(0, 0, 0, 0);
    containerRef.current.appendChild(gl.canvas);

    const camera = new Camera(gl);
    const scene = new Transform();

    const geometry = new Geometry(gl, { position: { size: 2, data: new Float32Array([-1, -1, 3, -1, -1, 3]) } });

    const program = new Program(gl, {
      vertex,
      fragment,
      uniforms: {
        iTime: { value: 0 },
        iResolution: { value: uniformResolution.current },
        uOffset: { value: uniformOffset.current },
        uRotation: { value: 0 },
        focalLength: { value: focalLength },
        speed1: { value: speed1 },
        speed2: { value: speed2 },
        dir2: { value: dir2 },
        bend1: { value: bend1 },
        bend2: { value: bend2 },
        bendAdj1: { value: 0 },
        bendAdj2: { value: 0 },
        uOpacity: { value: 0 }
      }
    });
    new Mesh(gl, { geometry, program }).setParent(scene);

    const applySize = () => {
      const el = containerRef.current;
      if (!el) return;
      const { width, height } = el.getBoundingClientRect();
      const rw = width * renderer.dpr,
        rh = height * renderer.dpr;
      if (rw === uniformResolution.current[0] && rh === uniformResolution.current[1]) return;
      renderer.setSize(width, height);
      uniformResolution.current[0] = rw;
      uniformResolution.current[1] = rh;
      gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
    };

    applySize();

    const resize = () => {
      if (resizeTimeoutRef.current) clearTimeout(resizeTimeoutRef.current);
      resizeTimeoutRef.current = setTimeout(() => {
        applySize();
        resizeTimeoutRef.current = null;
      }, 150);
    };
    const ro = new ResizeObserver(resize);
    ro.observe(containerRef.current);

    startTimeRef.current = performance.now();

    const loop = now => {
      const {
        xOffset: xOff,
        yOffset: yOff,
        rotationDeg: rot,
        focalLength: fLen,
        fadeInDuration: fadeDur
      } = propsRef.current;
      const t = (now - startTimeRef.current) * 0.001;
      if (fadeStartTime.current === null && t > 0.1) fadeStartTime.current = now;
      let opacity = 0;
      if (fadeStartTime.current !== null) {
        const fe = now - fadeStartTime.current;
        opacity = Math.min(fe / fadeDur, 1);
        opacity = 1 - Math.pow(1 - opacity, 3);
      }
      uniformOffset.current[0] = xOff;
      uniformOffset.current[1] = yOff;
      program.uniforms.iTime.value = t;
      program.uniforms.uRotation.value = (rot * Math.PI) / 180;
      program.uniforms.focalLength.value = fLen;
      program.uniforms.uOpacity.value = opacity;
      renderer.render({ scene, camera });
      if (runningRef.current) rafRef.current = requestAnimationFrame(loop);
    };
    const start = () => {
      if (runningRef.current || permaPausedRef.current) return;
      runningRef.current = true;
      startTimeRef.current = performance.now() - program.uniforms.iTime.value * 1000;
      if (dynamicDpr) {
        const target = Math.min(window.devicePixelRatio, 1);
        if (renderer.dpr !== target) renderer.dpr = target;
      }
      renderer.render({ scene, camera });
      rafRef.current = requestAnimationFrame(loop);
    };
    const stop = () => {
      runningRef.current = false;
      if (rafRef.current) cancelAnimationFrame(rafRef.current);
    };
    startStopApiRef.current = { start, stop };

    start();

    const containerEl = containerRef.current;
    if (pauseWhenOffscreen && 'IntersectionObserver' in window) {
      observerRef.current = new IntersectionObserver(
        e => {
          const n = e[0];
          if (!n) return;
          if (n.isIntersecting) start();
          else stop();
        },
        { root: null, rootMargin, threshold: 0 }
      );
      if (containerEl) observerRef.current.observe(containerEl);
    }

    return () => {
      runningRef.current = false;
      if (rafRef.current) cancelAnimationFrame(rafRef.current);
      ro.disconnect();
      if (resizeTimeoutRef.current) {
        clearTimeout(resizeTimeoutRef.current);
        resizeTimeoutRef.current = null;
      }
      if (observerRef.current && containerEl) {
        if (typeof observerRef.current.unobserve === 'function') observerRef.current.unobserve(containerEl);
        observerRef.current.disconnect();
        observerRef.current = null;
      }
      renderer.gl.canvas.remove();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isMobile, fallbackActive]);

  useEffect(() => {
    if (isMobile || fallbackActive || !autoPauseOnScroll) return;
    if (!appliedScrollThresholdRef.current)
      appliedScrollThresholdRef.current = scrollPauseThreshold ?? Math.round(window.innerHeight * 1.2);
    const limit = appliedScrollThresholdRef.current;
    const onScroll = () => {
      const y = window.scrollY || window.pageYOffset;
      if (!permaPausedRef.current && y > limit) {
        startStopApiRef.current.stop();
        if (!resumeOnScrollUp) permaPausedRef.current = true;
      } else if (resumeOnScrollUp && y <= limit && !permaPausedRef.current) {
        startStopApiRef.current.start();
      }
    };
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
    return () => window.removeEventListener('scroll', onScroll);
  }, [isMobile, fallbackActive, autoPauseOnScroll, scrollPauseThreshold, resumeOnScrollUp]);

  if (isMobile) return null;

  if (fallbackActive) {
    return (
      <div
        ref={containerRef}
        style={{
          position: 'absolute',
          inset: 0,
          overflow: 'hidden',
          width: '100vw',
          height: '100vh',
          pointerEvents: 'none',
          background:
            'radial-gradient(1200px 600px at 20% 80%, rgba(123, 31, 162, 0.3), transparent 60%), radial-gradient(1000px 500px at 80% 20%, rgba(33, 150, 243, 0.25), transparent 60%), linear-gradient(180deg, rgba(10, 2, 20, 0.6), rgba(10, 2, 20, 0.8))'
        }}
      >
        <div
          style={{
            position: 'absolute',
            bottom: 0,
            left: 0,
            right: 0,
            height: 200,
            background: 'linear-gradient(to top, #060010, transparent)',
            pointerEvents: 'none',
            zIndex: 1
          }}
        />
      </div>
    );
  }

  return (
    <div
      ref={containerRef}
      style={{
        opacity: 0.6,
        position: 'absolute',
        inset: 0,
        overflow: 'hidden',
        width: '100vw',
        height: '100vh',
        pointerEvents: 'none',
        willChange: 'opacity'
      }}
    >
      <div
        style={{
          position: 'absolute',
          bottom: 0,
          left: 0,
          right: 0,
          height: 200,
          background: 'linear-gradient(to top, #060010, transparent)',
          pointerEvents: 'none',
          zIndex: 1
        }}
      />
    </div>
  );
}
