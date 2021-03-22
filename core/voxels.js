class VoxelWorld {
  constructor({
    wasm,
    chunkSize,
    width,
    height,
    depth,
    onLoad,
  }) {
    this.chunkSize = chunkSize;
    this.width = width;
    this.height = height;
    this.depth = depth;
    this.simulationStep = 0;
    const maxFaces = Math.ceil(chunkSize * chunkSize * chunkSize * 0.5) * 6; // worst possible case
    const queueSize = width * depth * 2;
    const layout = [
      { id: 'voxels', type: Uint8Array, size: width * height * depth * 5 },
      { id: 'vertices', type: Uint8Array, size: maxFaces * 4 * 6 },
      { id: 'indices', type: Uint32Array, size: maxFaces * 6 },
      { id: 'heightmap', type: Int32Array, size: width * depth },
      { id: 'queueA', type: Int32Array, size: queueSize },
      { id: 'queueB', type: Int32Array, size: queueSize },
      { id: 'queueC', type: Int32Array, size: queueSize },
      { id: 'world', type: Int32Array, size: 3 },
      { id: 'bounds', type: Float32Array, size: 4 },
    ];
    const pages = Math.ceil(layout.reduce((total, { type, size }) => (
      total + size * type.BYTES_PER_ELEMENT
    ), 0) / 65536) + 1;
    const memory = new WebAssembly.Memory({ initial: pages, maximum: pages });
    (WebAssembly.instantiateStreaming ? (
      WebAssembly.instantiateStreaming(fetch(wasm), { env: { memory } })
    ) : (
      fetch(wasm).then((res) => res.arrayBuffer()).then((buffer) => (
        WebAssembly.instantiate(buffer, { env: { memory } })
      ))
    ))
      .then(({ instance }) => {
        this._mesh = instance.exports.mesh;
        this._generate = instance.exports.generate;
        this._propagate = instance.exports.propagate;
        this._simulate = instance.exports.simulate;
        this._update = instance.exports.update;
        let address = instance.exports.__heap_base * 1;
        layout.forEach(({ id, type, size }) => {
          this[id] = {
            address,
            view: new type(memory.buffer, address, size),
          };
          address += size * type.BYTES_PER_ELEMENT;
        });
        this.world.view.set([width, height, depth]);
        if (onLoad) {
          onLoad(this);
        }
        this.hasLoaded = true;
      })
      .catch((e) => console.error(e));
  }

  mesh(x, y, z) {
    const {
      world,
      voxels,
      chunkSize,
      bounds,
      indices,
      vertices,
    } = this;
    const faces = this._mesh(
      world.address,
      voxels.address,
      bounds.address,
      indices.address,
      vertices.address,
      chunkSize,
      x * chunkSize,
      y * chunkSize,
      z * chunkSize
    );
    if (faces === -1) {
      throw new Error('Requested chunk is out of bounds');
    }
    return {
      bounds: new Float32Array(bounds.view),
      indices: new ((faces * 4 - 1) <= 65535 ? Uint16Array : Uint32Array)(
        indices.view.subarray(0, faces * 6)
      ),
      vertices: new Uint8Array(vertices.view.subarray(0, faces * 4 * 6)),
    };
  }

  generate({
    seed = Math.floor(Math.random() * 2147483647),
    type = 0,
    unlit = false,
  }) {
    const {
      world,
      heightmap,
      voxels,
      queueA,
      queueB,
    } = this;
    heightmap.view.fill(0);
    voxels.view.fill(0);
    this._generate(
      world.address,
      heightmap.address,
      voxels.address,
      seed,
      type
    );
    if (unlit) {
      // ToDo/Incomplete
      // This is a bit of a hack for the animation test.
      // The ideal solution will be keeping this light levels at 0
      // and then have an optional parameter in the mesher so it can
      // ignore the light levels when building the chunk faces
      for (let i = 4, l = voxels.view.length; i < l; i += 5) {
        voxels.view[i] = 32;
      }
    } else {
      this._propagate(
        world.address,
        heightmap.address,
        voxels.address,
        queueA.address,
        queueB.address
      );
    }
  }

  simulate(steps) {
    const {
      world,
      heightmap,
      voxels,
    } = this;
    for (let i = 0; i < steps; i += 1) {
      this._simulate(
        world.address,
        heightmap.address,
        voxels.address,
        this.simulationStep
      );
      this.simulationStep += 1;
    }
  }

  update({
    type,
    x, y, z,
    r, g, b,
  }) {
    const {
      world,
      heightmap,
      voxels,
      queueA,
      queueB,
      queueC,
    } = this;
    this._update(
      world.address,
      heightmap.address,
      voxels.address,
      queueA.address,
      queueB.address,
      queueC.address,
      type,
      x, y, z,
      r, g, b
    );
  }

  setupPakoWorker() {
    let requestId = 0;
    const requests = [];
    this.pako = new Worker('/vendor/pako.worker.js');
    this.pako.addEventListener('message', ({ data: { id, data } }) => {
      const req = requests.findIndex((p) => p.id === id);
      if (req !== -1) {
        requests.splice(req, 1)[0].resolve(data);
      }
    });
    this.pako.request = ({ data, operation }) => (
      new Promise((resolve) => {
        const id = requestId++;
        requests.push({ id, resolve });
        this.pako.postMessage({ id, data, operation }, [data.buffer]);
      })
    );
  }

  exportVoxels() {
    if (!this.pako) this.setupPakoWorker();
    const { voxels, pako } = this;
    return pako.request({ data: new Uint8Array(voxels.view), operation: 'deflate' });
  }

  importVoxels(deflated) {
    if (!this.pako) this.setupPakoWorker();
    const {
      width,
      height,
      depth,
      heightmap,
      voxels,
      pako,
    } = this;
    return pako.request({ data: deflated, operation: 'inflate' })
      .then((inflated) => {
        // This should prolly be a method in the C implementation
        for (let z = 0, index = 0; z < depth; z += 1) {
          for (let x = 0; x < width; x += 1, index += 1) {
            for (let y = height - 1; y >= 0; y -= 1) {
              if (
                y === 0
                || inflated[(z * width * height + y * width + x) * 5] !== 0
              ) {
                heightmap.view[index] = y;
                break;
              }
            }
          }
        }
        voxels.view.set(inflated);
      });
  }
}

export default VoxelWorld;
