import {
  Color,
  Mesh,
  PlaneBufferGeometry,
  ShaderMaterial,
} from '../vendor/three.js';

class Grid extends Mesh {
  static setupGeometry() {
    Grid.geometry = new PlaneBufferGeometry(512, 512);
    Grid.geometry.rotateX(Math.PI * -0.5);
  }

  static setupMaterial() {
    Grid.material = new ShaderMaterial({
      uniforms: {
        fogColor: { value: new Color(0x0a141e) },
      },
      vertexShader: [
        'varying vec2 gridPosition;',
        'varying vec3 fragPosition;',
        'void main() {',
        '  gridPosition = vec3(modelMatrix * vec4(position, 1.0)).xz;',
        '  fragPosition = vec3(modelViewMatrix * vec4(position.x, cameraPosition.y, position.z, 1.0));',
        '  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);',
        '}',
      ].join('\n'),
      fragmentShader: [
        'varying vec2 gridPosition;',
        'varying vec3 fragPosition;',
        'const vec3 chunkColor = vec3(0.0, 0.7, 0.0);',
        'const vec3 voxelsColor = vec3(0.7);',
        'const float fogDensity = 0.02;',
        'uniform vec3 fogColor;',
        'float line(vec2 position) {',
        '  vec2 coord = abs(fract(position - 0.5) - 0.5) / fwidth(position);',
        '  return 1.0 - min(min(coord.x, coord.y), 1.0);',
        '}',
        'void main() {',
        '  vec3 grid = chunkColor * line(gridPosition / 16.0) + voxelsColor * line(gridPosition * 2.0);',
        '  float fogDepth = length(fragPosition);',
        '  float fogFactor = 1.0 - exp( - fogDensity * fogDensity * fogDepth * fogDepth );',
        '  gl_FragColor = vec4(mix(grid, fogColor, fogFactor), 1.0);',
        '}',
      ].join('\n'),
    });
  }

  constructor({ x, z }) {
    if (!Grid.geometry) {
      Grid.setupGeometry();
    }
    if (!Grid.material) {
      Grid.setupMaterial();
    }
    super(Grid.geometry, Grid.material);
    this.position.set(x, 0, z);
    this.updateMatrixWorld();
    this.matrixAutoUpdate = false;
  }
}

export default Grid;
