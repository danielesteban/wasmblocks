import {
  BufferGeometry,
  Mesh,
  BufferAttribute,
  InterleavedBuffer,
  InterleavedBufferAttribute,
  ShaderLib,
  ShaderMaterial,
  Sphere,
  UniformsUtils,
} from '../vendor/three.js';

class VoxelChunk extends Mesh {
  static setupMaterial() {
    const { uniforms, vertexShader, fragmentShader } = ShaderLib.basic;
    VoxelChunk.material = new ShaderMaterial({
      uniforms: {
        ...UniformsUtils.clone(uniforms),
      },
      vertexShader: vertexShader
        .replace(
          '#include <color_vertex>',
          [
            '#ifdef USE_COLOR',
            '  vColor.xyz = color.xyz / 255.0;',
            '#endif',
          ].join('\n')
        ),
      fragmentShader,
      vertexColors: true,
    });
  }

  constructor({
    x, y, z,
    geometry,
    scale,
  }) {
    if (!VoxelChunk.material) {
      VoxelChunk.setupMaterial();
    }
    super(new BufferGeometry(), VoxelChunk.material);
    if (geometry.indices.length) {
      this.update(geometry);
    }
    this.position.set(x, y, z).multiplyScalar(scale);
    this.scale.setScalar(scale);
    this.updateMatrixWorld();
    this.matrixAutoUpdate = false;
  }

  update({ bounds, indices, vertices }) {
    const { geometry } = this;
    vertices = new InterleavedBuffer(vertices, 6);
    geometry.setIndex(new BufferAttribute(indices, 1));
    geometry.setAttribute('position', new InterleavedBufferAttribute(vertices, 3, 0));
    geometry.setAttribute('color', new InterleavedBufferAttribute(vertices, 3, 3));
    if (geometry.boundingSphere === null) {
			geometry.boundingSphere = new Sphere();
		}
    geometry.boundingSphere.set({ x: bounds[0], y: bounds[1], z: bounds[2] }, bounds[3]);
  }

  dispose() {
    const { geometry } = this;
    geometry.dispose();
  }
}

export default VoxelChunk;
