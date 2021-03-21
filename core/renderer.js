import {
  ACESFilmicToneMapping,
  Clock,
  PerspectiveCamera,
  ShaderChunk,
  sRGBEncoding,
  WebGLRenderer,
} from '../vendor/three.js';
import Controls from './controls.js';

class Renderer {
  constructor(dom) {
    this.camera = new PerspectiveCamera(70, 1, 0.1, 1000);
    this.clock = new Clock();
    this.controls = new Controls({ camera: this.camera, dom: dom.renderer });
    this.fps = {
      count: 0,
      dom: dom.fps,
      lastTick: this.clock.oldTime / 1000,
    };
    this.renderer = new WebGLRenderer({
      antialias: true,
      stencil: false,
      powerPreference: 'high-performance',
    });
    this.renderer.outputEncoding = sRGBEncoding;
    this.renderer.toneMapping = ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.8;
    this.renderer.setPixelRatio(window.devicePixelRatio || 1);
    this.renderer.setAnimationLoop(this.onAnimationTick.bind(this));
    dom.renderer.appendChild(this.renderer.domElement);
    window.addEventListener('resize', this.onResize.bind(this), false);
    this.onResize();
  }

  onAnimationTick() {
    const {
      camera,
      controls,
      clock,
      fps,
      renderer,
      scene,
    } = this;

    const animation = {
      delta: Math.min(clock.getDelta(), 1 / 30),
      time: clock.oldTime / 1000,
    };

    if (scene) {
      controls.onAnimationTick(animation);
      scene.onAnimationTick(animation);
      renderer.render(scene, camera);
    }

    fps.count += 1;
    if (animation.time >= fps.lastTick + 1) {
      renderer.fps = Math.round(fps.count / (animation.time - fps.lastTick));
      fps.dom.innerText = `${renderer.fps}fps`;
      fps.lastTick = animation.time;
      fps.count = 0;
    }
  }

  onResize() {
    const { camera, renderer } = this;
    const { innerWidth: width, innerHeight: height } = window;
    renderer.setSize(width, height);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }
}

export default Renderer;
