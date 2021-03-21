import {
  AudioListener,
  Raycaster,
  Vector2,
  Vector3,
} from '../vendor/three.js';

class Controls {
  constructor({ camera, dom }) {
    this.aux = {
      center: new Vector2(),
      direction: new Vector3(),
      forward: new Vector3(),
      right: new Vector3(),
      worldUp: new Vector3(0, 1, 0),
    };
    this.brush = {
      shape: Controls.brushShapes.sphere,
      size: 6,
      noise: 0.3,
    };
    this.brushes = new Map();
    this.buttons = {
      primary: false,
      secondary: false,
      tertiary: false,
    };
    this.buttonState = { ...this.buttons };
    this.listener = new AudioListener();
    camera.add(this.listener);
    camera.rotation.order = 'YXZ';
    this.camera = camera;
    this.dom = dom;
    this.keyboard = new Vector3(0, 0, 0);
    this.pointer = new Vector2(0, 0);
    this.raycaster = new Raycaster();
    this.raycaster.far = 96;
    this.speed = 6;
    this.onBlur = this.onBlur.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onKeyUp = this.onKeyUp.bind(this);
    this.onMouseDown = this.onMouseDown.bind(this);
    this.onMouseMove = this.onMouseMove.bind(this);
    this.onMouseUp = this.onMouseUp.bind(this);
    this.onMouseWheel = this.onMouseWheel.bind(this);
    this.onPointerLock = this.onPointerLock.bind(this);
    this.requestPointerLock = this.requestPointerLock.bind(this);
    window.addEventListener('blur', this.onBlur, false);
    document.addEventListener('keydown', this.onKeyDown, false);
    document.addEventListener('keyup', this.onKeyUp, false);
    document.addEventListener('mousedown', this.onMouseDown, false);
    document.addEventListener('mousemove', this.onMouseMove, false);
    document.addEventListener('mouseup', this.onMouseUp, false);
    document.addEventListener('wheel', this.onMouseWheel, false);
    document.addEventListener('pointerlockchange', this.onPointerLock, false);
    dom.addEventListener('mousedown', this.requestPointerLock, false);
  }

  dispose() {
    const { dom, camera, isLocked, listener } = this;
    camera.remove(listener);
    document.body.classList.remove('pointerlock');
    window.removeEventListener('blur', this.onBlur);
    document.removeEventListener('keydown', this.onKeyDown);
    document.removeEventListener('keyup', this.onKeyUp);
    dom.removeEventListener('mousedown', this.onMouseDown);
    document.removeEventListener('mousemove', this.onMouseMove);
    document.removeEventListener('wheel', this.onMouseWheel);
    document.removeEventListener('pointerlockchange', this.onPointerLock);
    if (isLocked) {
      document.exitPointerLock();
    }
  }

  getBrush({ shape, size }) {
    const { brushShapes } = Controls;
    const { brushes } = this;
    const key = `${shape}:${size}`;
    let brush = brushes.get(key);
    if (!brush) {
      brush = [];
      if (shape === brushShapes.box) {
        size -= 1;
      }
      const radius = Math.sqrt(((size * 0.5) ** 2) * 3);
      for (let z = -size; z <= size; z += 1) {
        for (let y = -size; y <= size; y += 1) {
          for (let x = -size; x <= size; x += 1) {
            if (
              shape === brushShapes.box
              || Math.sqrt(x ** 2 + y ** 2 + z ** 2) <= radius
            ) {
              brush.push({ x, y, z });
            }
          }
        }
      }
      brush.sort((a, b) => (
        Math.sqrt(a.x ** 2 + a.y ** 2 + a.z ** 2) - Math.sqrt(b.x ** 2 + b.y ** 2 + b.z ** 2)
      ));
      brushes.set(key, brush);
    }
    return brush;
  }

  onAnimationTick({ delta }) {
    const {
      aux,
      buttons,
      buttonState,
      camera,
      keyboard,
      isLocked,
      pointer,
      raycaster,
      speed
    } = this;
    if (!isLocked) {
      return;
    }
    if (pointer.x !== 0 || pointer.y !== 0) {
      camera.rotation.y -= pointer.x * delta * 0.05;
      camera.rotation.x -= pointer.y * delta * 0.05;
      const PI_2 = Math.PI / 2;
      camera.rotation.x = Math.max(-PI_2, Math.min(PI_2, camera.rotation.x));
      pointer.set(0, 0);
    }
    if (keyboard.x !== 0 || keyboard.y !== 0 || keyboard.z !== 0) {
      const {
        direction,
        forward,
        right,
        worldUp,
      } = this.aux;
      forward.set(0, 0, -1).applyQuaternion(camera.quaternion).normalize();
      right.crossVectors(forward, worldUp).normalize();
      camera.position.add(
        direction
          .set(0, 0, 0)
          .addScaledVector(right, keyboard.x)
          .addScaledVector(worldUp, keyboard.y)
          .addScaledVector(forward, keyboard.z)
          .normalize()
          .multiplyScalar(delta * speed)
      );
    }
    ['primary', 'secondary', 'tertiary'].forEach((button) => {
      const state = buttonState[button];
      buttons[`${button}Down`] = state && buttons[button] !== state;
      buttons[`${button}Up`] = !state && buttons[button] !== state;
      buttons[button] = state;
    });
    raycaster.setFromCamera(aux.center, camera);
  }

  onBlur() {
    const { buttonState, keyboard } = this;
    buttonState.primary = false;
    buttonState.secondary = false;
    buttonState.tertiary = false;
    this.buttons = { ...buttonState };
    keyboard.set(0, 0, 0);
  }

  onKeyDown({ keyCode, repeat }) {
    const { keyboard } = this;
    if (repeat) return;
    switch (keyCode) {
      case 16:
        if (keyboard.y === 0) keyboard.y = -1;
        break;
      case 32:
        if (keyboard.y === 0) keyboard.y = 1;
        break;
      case 87:
        if (keyboard.z === 0) keyboard.z = 1;
        break;
      case 83:
        if (keyboard.z === 0) keyboard.z = -1;
        break;
      case 65:
        if (keyboard.x === 0) keyboard.x = -1;
        break;
      case 68:
        if (keyboard.x === 0) keyboard.x = 1;
        break;
      default:
        break;
    }
  }

  onKeyUp({ keyCode, repeat }) {
    const { keyboard } = this;
    if (repeat) return;
    switch (keyCode) {
      case 16:
        if (keyboard.y < 0) keyboard.y = 0;
        break;
      case 32:
        if (keyboard.y > 0) keyboard.y = 0;
        break;
      case 87:
        if (keyboard.z > 0) keyboard.z = 0;
        break;
      case 83:
        if (keyboard.z < 0) keyboard.z = 0;
        break;
      case 65:
        if (keyboard.x < 0) keyboard.x = 0;
        break;
      case 68:
        if (keyboard.x > 0) keyboard.x = 0;
        break;
      default:
        break;
    }
  }

  onMouseDown({ button }) {
    const { buttonState, isLocked } = this;
    if (!isLocked) {
      return;
    }
    switch (button) {
      case 0:
        buttonState.primary = true;
        break;
      case 1:
        buttonState.tertiary = true;
        break;
      case 2:
        buttonState.secondary = true;
        break;
      default:
        break;
    }
  }

  onMouseMove({ movementX, movementY }) {
    const { isLocked, pointer } = this;
    if (!isLocked) {
      return;
    }
    pointer.add({ x: movementX, y: movementY });
  }

  onMouseUp({ button }) {
    const { buttonState, isLocked } = this;
    if (!isLocked) {
      return;
    }
    switch (button) {
      case 0:
        buttonState.primary = false;
        break;
      case 1:
        buttonState.tertiary = false;
        break;
      case 2:
        buttonState.secondary = false;
        break;
      default:
        break;
    }
  }

  onMouseWheel({ deltaY }) {
    const { speed, isLocked } = this;
    if (!isLocked) {
      return;
    }
    const { minSpeed, speedRange } = Controls;
    const logSpeed = Math.min(
      Math.max(
        ((Math.log(speed) - minSpeed) / speedRange) - (-deltaY * 0.0003),
        0
      ),
      1
    );
    this.speed = Math.exp(minSpeed + logSpeed * speedRange);
  }

  onPointerLock() {
    this.isLocked = !!document.pointerLockElement;
    document.body.classList[this.isLocked ? 'add' : 'remove']('pointerlock');
    if (!this.isLocked) {
      this.onBlur();
    }
  }

  requestPointerLock() {
    const { isLocked, listener } = this;
    if (isLocked) {
      return;
    }
    if (listener.context.state === 'suspended') {
      listener.context.resume();
    }
    document.body.requestPointerLock();
  }
}

Controls.brushShapes = { box: 0, sphere: 1 };
Controls.minSpeed = Math.log(2);
Controls.maxSpeed = Math.log(40);
Controls.speedRange = Controls.maxSpeed - Controls.minSpeed;

export default Controls;
