#define FNL_IMPL
#include "../vendor/FastNoiseLite.h"

enum BlockTypes {
  TYPE_AIR,
  TYPE_STONE,
  TYPE_LIGHT,
  TYPE_SAND
};

enum VoxelFields {
  VOXEL_TYPE,
  VOXEL_R,
  VOXEL_G,
  VOXEL_B,
  VOXEL_LIGHT,
  VOXEL_SUNLIGHT,
  VOXELS_STRIDE
};

typedef struct {
  const int width;
  const int height;
  const int depth;
} World;

static const unsigned char maxLight = 32;

static const int neighbors[] = {
  1, 0, 0,
  -1, 0, 0,
  0, 0, 1,
  0, 0, -1,
  0, 1, 0,
  0, -1, 0
};

static const int getVoxel(
  const World* world,
  const int x,
  const int y,
  const int z
) {
  if (
    x < 0 || x >= world->width
    || y < 0 || y >= world->height
    || z < 0 || z >= world->depth
  ) {
    return -1;
  }
  return (z * world->width * world->height + y * world->width + x) * VOXELS_STRIDE;
}

static const unsigned int getColorFromNoise(unsigned char noise) {
  noise = 255 - noise;
  if (noise < 85) {
    return (
      ((255 - noise * 3) << 16)
      | (0 << 8)
      | (noise * 3)
    );
  }
  if (noise < 170) {
    noise -= 85;
    return (
      (0 << 16)
      | ((noise * 3) << 8)
      | (255 - noise * 3)
    );
  }
  noise -= 170;
  return (
    ((noise * 3) << 16)
    | ((255 - noise * 3) << 8)
    | 0
  );
}

static const unsigned int getLight(
  const unsigned char* voxels,
  const unsigned char light,
  const unsigned char sunlight,
  const int n1,
  const int n2,
  const int n3
) {
  unsigned char ao = 0;
  {
    const unsigned char v1 = (n1 != -1 && voxels[n1] != TYPE_AIR) ? 1 : 0,
                        v2 = (n2 != -1 && voxels[n2] != TYPE_AIR) ? 1 : 0,
                        v3 = (n3 != -1 && voxels[n3] != TYPE_AIR) ? 1 : 0;
    if (v1 == 1) ao += 20;
    if (v2 == 1) ao += 20;
    if ((v1 == 1 && v2 == 1) || v3 == 1) ao += 20;
  }

  float avgLight = light;
  float avgSunlight = sunlight;
  {
    const unsigned char v1 = (n1 != -1 && voxels[n1] == TYPE_AIR) ? 1 : 0,
                        v2 = (n2 != -1 && voxels[n2] == TYPE_AIR) ? 1 : 0,
                        v3 = (n3 != -1 && voxels[n3] == TYPE_AIR) ? 1 : 0;
    unsigned char n = 1;
    if (v1 == 1) {
      avgLight += voxels[n1 + VOXEL_LIGHT];
      avgSunlight += voxels[n1 + VOXEL_SUNLIGHT];
      n++;
    }
    if (v2 == 1) {
      avgLight += voxels[n2 + VOXEL_LIGHT];
      avgSunlight += voxels[n2 + VOXEL_SUNLIGHT];
      n++;
    }
    if ((v1 == 1 || v2 == 1) && v3 == 1) {
      avgLight += voxels[n3 + VOXEL_LIGHT];
      avgSunlight += voxels[n3 + VOXEL_SUNLIGHT];
      n++;
    }
    avgLight = avgLight / n / maxLight * 0xFF;
    avgSunlight = avgSunlight / n / maxLight * 0xFF;
  }
  return (
    (ao << 16) | (((unsigned char) avgLight) << 8) | ((unsigned char) avgSunlight)
  );
}

static void floodLight(
  const unsigned char channel,
  const World* world,
  const int* heightmap,
  unsigned char* voxels,
  int* queue,
  const unsigned int size,
  int* next
) {
  unsigned int nextLength = 0;
  for (unsigned int i = 0; i < size; i++) {
    const int voxel = queue[i];
    const unsigned char light = voxels[voxel + channel];
    if (light == 0) {
      continue;
    }
    const int index = voxel / VOXELS_STRIDE,
              z = _fnlFastFloor(index / (world->width * world->height)),
              y = _fnlFastFloor((index % (world->width * world->height)) / world->width),
              x = _fnlFastFloor((index % (world->width * world->height)) % world->width);
    for (unsigned char n = 0; n < 6; n += 1) {
      const int nx = x + neighbors[n * 3],
                ny = y + neighbors[n * 3 + 1],
                nz = z + neighbors[n * 3 + 2],
                neighbor = getVoxel(world, nx, ny, nz);
      const unsigned char nl = channel == VOXEL_SUNLIGHT && n == 5 && light == maxLight ? (
        light
      ) : (
        light - 1
      );
      if (
        neighbor == -1
        || voxels[neighbor] != TYPE_AIR
        || (
          channel == VOXEL_SUNLIGHT
          && n != 5
          && light == maxLight
          && ny > heightmap[(nz * world->width) + nx]
        )
        || voxels[neighbor + channel] >= nl
      ) {
        continue;
      }
      voxels[neighbor + channel] = nl;
      next[nextLength++] = neighbor;
    }
  }
  if (nextLength > 0) {
    floodLight(
      channel,
      world,
      heightmap,
      voxels,
      next,
      nextLength,
      queue
    );
  }
}

static void removeLight(
  const unsigned char channel,
  const World* world,
  const int* heightmap,
  unsigned char* voxels,
  int* queue,
  const unsigned int size,
  int* next,
  int* floodQueue,
  unsigned int floodQueueSize
) {
  unsigned int nextLength = 0;
  for (int i = 0; i < size; i += 2) {
    const int voxel = queue[i];
    const unsigned char light = queue[i + 1];
    const int index = voxel / VOXELS_STRIDE,
              z = _fnlFastFloor(index / (world->width * world->height)),
              y = _fnlFastFloor((index % (world->width * world->height)) / world->width),
              x = _fnlFastFloor((index % (world->width * world->height)) % world->width);
    for (unsigned char n = 0; n < 6; n += 1) {
      const int neighbor = getVoxel(
        world,
        x + neighbors[n * 3],
        y + neighbors[n * 3 + 1],
        z + neighbors[n * 3 + 2]
      );
      if (neighbor == -1 || voxels[neighbor] != TYPE_AIR) {
        continue;
      }
      const unsigned char nl = voxels[neighbor + channel];
      if (nl == 0) {
        continue;
      }
      if (
        nl < light
        || (
          channel == VOXEL_SUNLIGHT
          && n == 5
          && light == maxLight
          && nl == maxLight
        )
      ) {
        next[nextLength++] = neighbor;
        next[nextLength++] = nl;
        voxels[neighbor + channel] = 0;
      } else if (nl >= light) {
        floodQueue[floodQueueSize++] = neighbor;
      }
    }
  }
  if (nextLength > 0) {
    removeLight(
      channel,
      world,
      heightmap,
      voxels,
      next,
      nextLength,
      queue,
      floodQueue,
      floodQueueSize
    );
  } else if (floodQueueSize > 0) {
    floodLight(
      channel,
      world,
      heightmap,
      voxels,
      floodQueue,
      floodQueueSize,
      queue
    );
  }
}

static void growBox(
  unsigned char* box,
  const unsigned char x,
  const unsigned char y,
  const unsigned char z
) {
  if (box[0] > x) box[0] = x;
  if (box[1] > y) box[1] = y;
  if (box[2] > z) box[2] = z;
  if (box[3] < x) box[3] = x;
  if (box[4] < y) box[4] = y;
  if (box[5] < z) box[5] = z;
}

static void pushFace(
  unsigned char* box,
  unsigned int* faces,
  unsigned int* indices,
  unsigned char* vertices,
  const int chunkX, const int chunkY, const int chunkZ,
  const unsigned char r, const unsigned char g, const unsigned char b,
  const int wx1, const int wy1, const int wz1, const unsigned int l1,
  const int wx2, const int wy2, const int wz2, const unsigned int l2,
  const int wx3, const int wy3, const int wz3, const unsigned int l3,
  const int wx4, const int wy4, const int wz4, const unsigned int l4
) {
  const float ao1 = ((l1 >> 16) & 0xFF) / 255.0f,
                      ao2 = ((l2 >> 16) & 0xFF) / 255.0f,
                      ao3 = ((l3 >> 16) & 0xFF) / 255.0f,
                      ao4 = ((l4 >> 16) & 0xFF) / 255.0f;
  const unsigned int  vertex = *faces * 4,
                      vertexOffset = vertex * 8,
                      indexOffset = *faces * 6,
                      flipFace = ao1 + ao3 > ao2 + ao4 ? 1 : 0; // Fixes interpolation anisotropy
  const unsigned char x1 = wx1 - chunkX,
                      y1 = wy1 - chunkY,
                      z1 = wz1 - chunkZ,
                      x2 = wx2 - chunkX,
                      y2 = wy2 - chunkY,
                      z2 = wz2 - chunkZ,
                      x3 = wx3 - chunkX,
                      y3 = wy3 - chunkY,
                      z3 = wz3 - chunkZ,
                      x4 = wx4 - chunkX,
                      y4 = wy4 - chunkY,
                      z4 = wz4 - chunkZ;
  (*faces)++;
  // Is this crazy? I dunno. You tell me.
  vertices[vertexOffset] = x1;
  vertices[vertexOffset + 1] = y1;
  vertices[vertexOffset + 2] = z1;
  vertices[vertexOffset + 3] = r * (1.0f - ao1);
  vertices[vertexOffset + 4] = g * (1.0f - ao1);
  vertices[vertexOffset + 5] = b * (1.0f - ao1);
  vertices[vertexOffset + 6] = (l1 >> 8) & 0xFF;
  vertices[vertexOffset + 7] = l1 & 0xFF;
  vertices[vertexOffset + 8] = x2;
  vertices[vertexOffset + 9] = y2;
  vertices[vertexOffset + 10] = z2;
  vertices[vertexOffset + 11] = r * (1.0f - ao2);
  vertices[vertexOffset + 12] = g * (1.0f - ao2);
  vertices[vertexOffset + 13] = b * (1.0f - ao2);
  vertices[vertexOffset + 14] = (l2 >> 8) & 0xFF;
  vertices[vertexOffset + 15] = l2 & 0xFF;
  vertices[vertexOffset + 16] = x3;
  vertices[vertexOffset + 17] = y3;
  vertices[vertexOffset + 18] = z3;
  vertices[vertexOffset + 19] = r * (1.0f - ao3);
  vertices[vertexOffset + 20] = g * (1.0f - ao3);
  vertices[vertexOffset + 21] = b * (1.0f - ao3);
  vertices[vertexOffset + 22] = (l3 >> 8) & 0xFF;
  vertices[vertexOffset + 23] = l3 & 0xFF;
  vertices[vertexOffset + 24] = x4;
  vertices[vertexOffset + 25] = y4;
  vertices[vertexOffset + 26] = z4;
  vertices[vertexOffset + 27] = r * (1.0f - ao4);
  vertices[vertexOffset + 28] = g * (1.0f - ao4);
  vertices[vertexOffset + 29] = b * (1.0f - ao4);
  vertices[vertexOffset + 30] = (l4 >> 8) & 0xFF;
  vertices[vertexOffset + 31] = l4 & 0xFF;
  indices[indexOffset] = vertex + flipFace;
  indices[indexOffset + 1] = vertex + flipFace + 1;
  indices[indexOffset + 2] = vertex + flipFace + 2;
  indices[indexOffset + 3] = vertex + flipFace + 2;
  indices[indexOffset + 4] = vertex + ((flipFace + 3) % 4);
  indices[indexOffset + 5] = vertex + flipFace;
  growBox(box, x1, y1, z1);
  growBox(box, x2, y2, z2);
  growBox(box, x3, y3, z3);
  growBox(box, x4, y4, z4);
}

void generate(
  const World* world,
  int* heightmap,
  unsigned char* voxels,
  const int seed,
  const unsigned char type
) {
  fnl_state noise = fnlCreateState();
  noise.seed = seed;
  noise.fractal_type = FNL_FRACTAL_FBM;
  for (int z = 0, voxel = 0; z < world->depth; z++) {
    for (int y = 0; y < world->height; y++) {
      for (int x = 0; x < world->width; x++, voxel += VOXELS_STRIDE) {
        if (
          x < 32 || x >= world->width - 32
          || z < 32 || z >= world->depth - 32
        ) {
          continue;
        }
        const float n = _fnlFastAbs(fnlGetNoise3D(&noise, x, y, z));
        unsigned char isBlock = 0;
        switch (type) {
          case 0: // Default
            isBlock = (y <= (n * world->height)) ? 1 : 0;
            break;
          case 1: { // Sphere
            const int cx = world->width * 0.5 - x;
            const int cy = world->height * 0.5 - y;
            const int cz = world->depth * 0.5 - z;
            isBlock = (
              y < world->height - 32
              && n > 0.1f
              && (y < 8 || sqrt(cx * cx + cz * cz) >= world->width * 0.05f)
              && sqrt(cx * cx + cy * cy + cz * cz) <= world->width * 0.425f
            ) ? 1 : 0;
          }
            break;
        }
        if (isBlock == 1) {
          const unsigned int color = getColorFromNoise(0xFF * n);
          const int heightmapIndex = z * world->width + x;
          voxels[voxel] = TYPE_STONE;
          voxels[voxel + VOXEL_R] = (color >> 16) & 0xFF;
          voxels[voxel + VOXEL_G] = (color >> 8) & 0xFF;
          voxels[voxel + VOXEL_B] = color & 0xFF;
          if (heightmap[heightmapIndex] < y) {
            heightmap[heightmapIndex] = y;
          }
        }
      }
    }
  }
}

void propagate(
  const World* world,
  const int* heightmap,
  unsigned char* voxels,
  int* queueA,
  int* queueB
) {
  unsigned int queueSize = 0;
  for (int z = 0, voxel = 0; z < world->depth; z++) {
    for (int x = 0; x < world->width; x++, voxel += VOXELS_STRIDE) {
      const int voxel = getVoxel(world, x, world->height - 1, z);
      if (voxels[voxel] == TYPE_AIR) {
        voxels[voxel + VOXEL_SUNLIGHT] = maxLight;
        queueA[queueSize++] = voxel;
      }
    }
  }
  floodLight(
    VOXEL_SUNLIGHT,
    world,
    heightmap,
    voxels,
    queueA,
    queueSize,
    queueB
  );
}

static const int sandNeighbors[] = {
  0, 0,
  1, 0,
  -1, 0,
  0, 1,
  0, -1
};

void simulate(
  const World* world,
  const int* heightmap,
  unsigned char* voxels,
  const unsigned int step
) {
  // Be aware that running this will make the heightmap data invalid.
  // This method could prolly update it but since it's not needed for
  // the animation test I decided not update it here.
  const unsigned char invZ = ((step % 4) < 2) ? 1 : 0;
  const unsigned char invX = ((step % 2) == 0) ? 1 : 0;
  for (int y = 1; y < world->height; y++) {
    for (int sz = 2; sz < world->depth - 2; sz++) {
      const int z = invZ == 1 ? world->depth - 1 - sz : sz;
      for (int sx = 2; sx < world->width - 2; sx++) {
        const int x = invX == 1 ? world->width - 1 - sx : sx;
        const int voxel = getVoxel(world, x, y, z);
        if (voxels[voxel] != TYPE_SAND) {
          continue;
        }
        int neighbor;
        for (unsigned char n = 0; n < 10; n += 2) {
          neighbor = getVoxel(world, x + sandNeighbors[n], y - 1, z + sandNeighbors[n + 1]);
          if (neighbor != -1 && voxels[neighbor] == TYPE_AIR) {
            break;
          }
        }
        if (neighbor == -1 || voxels[neighbor] != TYPE_AIR) {
          voxels[voxel] = TYPE_STONE;
          continue;
        }
        voxels[neighbor] = voxels[voxel];
        voxels[neighbor + VOXEL_R] = voxels[voxel + VOXEL_R];
        voxels[neighbor + VOXEL_G] = voxels[voxel + VOXEL_G];
        voxels[neighbor + VOXEL_B] = voxels[voxel + VOXEL_B];
        voxels[voxel] = 0;
        voxels[voxel + VOXEL_R] = 0;
        voxels[voxel + VOXEL_G] = 0;
        voxels[voxel + VOXEL_B] = 0;
        for (int n = 0; n < 10; n += 2) {
          neighbor = getVoxel(world, x + sandNeighbors[n], y + 1, z + sandNeighbors[n + 1]);
          if (neighbor != -1 && voxels[neighbor] == TYPE_STONE) {
            voxels[neighbor] = TYPE_SAND;
          }
        }
      }
    }
  }
}

void update(
  const World* world,
  int* heightmap,
  unsigned char* voxels,
  int* queueA,
  int* queueB,
  int* queueC,
  const unsigned char type,
  const int x,
  const int y,
  const int z,
  const unsigned char r,
  const unsigned char g,
  const unsigned char b
) {
  if (
    x < 1 || x >= world->width - 1
    || y < 0 || y >= world->height - 1
    || z < 1 || z >= world->depth - 1
  ) {
    return;
  }
  const int voxel = getVoxel(world, x, y, z);
  const int heightmapIndex = z * world->width + x;
  const int height = heightmap[heightmapIndex];
  const unsigned char current = voxels[voxel];
  if (type == TYPE_AIR) {
    if (y == height) {
      for (int h = y - 1; h >= 0; h --) {
        if (h == 0 || voxels[getVoxel(world, x, h, z)] != TYPE_AIR) {
          heightmap[heightmapIndex] = h;
          break;
        }
      }
    }
  } else if (height < y) {
    heightmap[heightmapIndex] = y;
  }
  voxels[voxel] = type;
  voxels[voxel + VOXEL_R] = r;
  voxels[voxel + VOXEL_G] = g;
  voxels[voxel + VOXEL_B] = b;
  if (current == TYPE_LIGHT) {
    const unsigned char light = voxels[voxel + VOXEL_LIGHT];
    voxels[voxel + VOXEL_LIGHT] = 0;
    queueA[0] = voxel;
    queueA[1] = light;
    removeLight(
      VOXEL_LIGHT,
      world,
      heightmap,
      voxels,
      queueA,
      2,
      queueB,
      queueC,
      0
    );
  } else if (current == TYPE_AIR && type != TYPE_AIR) {
    const unsigned char light = voxels[voxel + VOXEL_LIGHT];
    if (light != 0) {
      voxels[voxel + VOXEL_LIGHT] = 0;
      queueA[0] = voxel;
      queueA[1] = light;
      removeLight(
        VOXEL_LIGHT,
        world,
        heightmap,
        voxels,
        queueA,
        2,
        queueB,
        queueC,
        0
      );
    }
    const unsigned char sunlight = voxels[voxel + VOXEL_SUNLIGHT];
    if (sunlight != 0) {
      voxels[voxel + VOXEL_SUNLIGHT] = 0;
      queueA[0] = voxel;
      queueA[1] = sunlight;
      removeLight(
        VOXEL_SUNLIGHT,
        world,
        heightmap,
        voxels,
        queueA,
        2,
        queueB,
        queueC,
        0
      );
    }
  }
  if (type == TYPE_LIGHT) {
    voxels[voxel + VOXEL_LIGHT] = maxLight;
    queueA[0] = voxel;
    floodLight(
      VOXEL_LIGHT,
      world,
      heightmap,
      voxels,
      queueA,
      1,
      queueB
    );
  } else if (type == TYPE_AIR && current != TYPE_AIR) {
    unsigned int lightQueue = 0;
    unsigned int sunlightQueue = 0;
    for (unsigned char n = 0; n < 6; n += 1) {
      const int neighbor = getVoxel(
        world,
        x + neighbors[n * 3],
        y + neighbors[n * 3 + 1],
        z + neighbors[n * 3 + 2]
      );
      if (neighbor != -1) {
        if (voxels[neighbor + VOXEL_LIGHT] != 0) {
          queueA[lightQueue++] = neighbor;
        }
        if (voxels[neighbor + VOXEL_SUNLIGHT] != 0) {
          queueB[sunlightQueue++] = neighbor;
        }
      }
    }
    if (lightQueue > 0) {
      floodLight(
        VOXEL_LIGHT,
        world,
        heightmap,
        voxels,
        queueA,
        lightQueue,
        queueC
      );
    }
    if (sunlightQueue > 0) {
      floodLight(
        VOXEL_SUNLIGHT,
        world,
        heightmap,
        voxels,
        queueB,
        sunlightQueue,
        queueC
      );
    }
    
  }
}

const int mesh(
  const World* world,
  const unsigned char* voxels,
  float* bounds,
  unsigned int* indices,
  unsigned char* vertices,
  const unsigned char chunkSize,
  const int chunkX,
  const int chunkY,
  const int chunkZ
) {
  if (
    chunkX < 0
    || chunkY < 0
    || chunkZ < 0
    || chunkX + chunkSize > world->width
    || chunkY + chunkSize > world->height
    || chunkZ + chunkSize > world->depth
  ) {
    return -1;
  }
  // WELCOME TO THE JUNGLE !!
  unsigned char box[6] = { chunkSize, chunkSize, chunkSize, 0, 0, 0 };
  unsigned int faces = 0;
  for (int z = chunkZ; z < chunkZ + chunkSize; z++) {
    for (int y = chunkY; y < chunkY + chunkSize; y++) {
      for (int x = chunkX; x < chunkX + chunkSize; x++) {
        const int voxel = getVoxel(world, x, y, z);
        if (voxels[voxel] == TYPE_AIR) {
          continue;
        }
        const unsigned char r = voxels[voxel + VOXEL_R],
                            g = voxels[voxel + VOXEL_G],
                            b = voxels[voxel + VOXEL_B];
        const int top = getVoxel(world, x, y + 1, z),
                  bottom = getVoxel(world, x, y - 1, z),
                  south = getVoxel(world, x, y, z + 1),
                  north = getVoxel(world, x, y, z - 1),
                  east = getVoxel(world, x + 1, y, z),
                  west = getVoxel(world, x - 1, y, z);
        if (top != -1 && voxels[top] == TYPE_AIR) {
          const unsigned char light = voxels[top + VOXEL_LIGHT];
          const unsigned char sunlight = voxels[top + VOXEL_SUNLIGHT];
          const int ts = getVoxel(world, x, y + 1, z + 1),
                    tn = getVoxel(world, x, y + 1, z - 1),
                    te = getVoxel(world, x + 1, y + 1, z),
                    tw = getVoxel(world, x - 1, y + 1, z);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y + 1, z + 1,
            getLight(voxels, light, sunlight, tw, ts, getVoxel(world, x - 1, y + 1, z + 1)),
            x + 1, y + 1, z + 1,
            getLight(voxels, light, sunlight, te, ts, getVoxel(world, x + 1, y + 1, z + 1)),
            x + 1, y + 1, z,
            getLight(voxels, light, sunlight, te, tn, getVoxel(world, x + 1, y + 1, z - 1)),
            x, y + 1, z,
            getLight(voxels, light, sunlight, tw, tn, getVoxel(world, x - 1, y + 1, z - 1))
          );
        }
        if (bottom != -1 && voxels[bottom] == TYPE_AIR) {
          const unsigned char light = voxels[bottom + VOXEL_LIGHT];
          const unsigned char sunlight = voxels[bottom + VOXEL_SUNLIGHT];
          const int bs = getVoxel(world, x, y - 1, z + 1),
                    bn = getVoxel(world, x, y - 1, z - 1),
                    be = getVoxel(world, x + 1, y - 1, z),
                    bw = getVoxel(world, x - 1, y - 1, z);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y, z,
            getLight(voxels, light, sunlight, bw, bn, getVoxel(world, x - 1, y - 1, z - 1)),
            x + 1, y, z,
            getLight(voxels, light, sunlight, be, bn, getVoxel(world, x + 1, y - 1, z - 1)),
            x + 1, y, z + 1,
            getLight(voxels, light, sunlight, be, bs, getVoxel(world, x + 1, y - 1, z + 1)),
            x, y, z + 1,
            getLight(voxels, light, sunlight, bw, bs, getVoxel(world, x - 1, y - 1, z + 1))
          );
        }
        if (south != -1 && voxels[south] == 0) {
          const unsigned char light = voxels[south + VOXEL_LIGHT];
          const unsigned char sunlight = voxels[south + VOXEL_SUNLIGHT];
          const int st = getVoxel(world, x, y + 1, z + 1),
                    sb = getVoxel(world, x, y - 1, z + 1),
                    se = getVoxel(world, x + 1, y, z + 1),
                    sw = getVoxel(world, x - 1, y, z + 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y, z + 1,
            getLight(voxels, light, sunlight, sw, sb, getVoxel(world, x - 1, y - 1, z + 1)),
            x + 1, y, z + 1,
            getLight(voxels, light, sunlight, se, sb, getVoxel(world, x + 1, y - 1, z + 1)),
            x + 1, y + 1, z + 1,
            getLight(voxels, light, sunlight, se, st, getVoxel(world, x + 1, y + 1, z + 1)),
            x, y + 1, z + 1,
            getLight(voxels, light, sunlight, sw, st, getVoxel(world, x - 1, y + 1, z + 1))
          );
        }
        if (north != -1 && voxels[north] == TYPE_AIR) {
          const unsigned char light = voxels[north + VOXEL_LIGHT];
          const unsigned char sunlight = voxels[north + VOXEL_SUNLIGHT];
          const int nt = getVoxel(world, x, y + 1, z - 1),
                    nb = getVoxel(world, x, y - 1, z - 1),
                    ne = getVoxel(world, x + 1, y, z - 1),
                    nw = getVoxel(world, x - 1, y, z - 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x + 1, y, z,
            getLight(voxels, light, sunlight, ne, nb, getVoxel(world, x + 1, y - 1, z - 1)),
            x, y, z,
            getLight(voxels, light, sunlight, nw, nb, getVoxel(world, x - 1, y - 1, z - 1)),
            x, y + 1, z,
            getLight(voxels, light, sunlight, nw, nt, getVoxel(world, x - 1, y + 1, z - 1)),
            x + 1, y + 1, z,
            getLight(voxels, light, sunlight, ne, nt, getVoxel(world, x + 1, y + 1, z - 1))
          );
        }
        if (east != -1 && voxels[east] == TYPE_AIR) {
          const unsigned char light = voxels[east + VOXEL_LIGHT];
          const unsigned char sunlight = voxels[east + VOXEL_SUNLIGHT];
          const int et = getVoxel(world, x + 1, y + 1, z),
                    eb = getVoxel(world, x + 1, y - 1, z),
                    es = getVoxel(world, x + 1, y, z + 1),
                    en = getVoxel(world, x + 1, y, z - 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x + 1, y, z + 1,
            getLight(voxels, light, sunlight, es, eb, getVoxel(world, x + 1, y - 1, z + 1)),
            x + 1, y, z,
            getLight(voxels, light, sunlight, en, eb, getVoxel(world, x + 1, y - 1, z - 1)),
            x + 1, y + 1, z,
            getLight(voxels, light, sunlight, en, et, getVoxel(world, x + 1, y + 1, z - 1)),
            x + 1, y + 1, z + 1,
            getLight(voxels, light, sunlight, es, et, getVoxel(world, x + 1, y + 1, z + 1))
          );
        }
        if (west != -1 && voxels[west] == TYPE_AIR) {
          const unsigned char light = voxels[west + VOXEL_LIGHT];
          const unsigned char sunlight = voxels[west + VOXEL_SUNLIGHT];
          const int wt = getVoxel(world, x - 1, y + 1, z),
                    wb = getVoxel(world, x - 1, y - 1, z),
                    ws = getVoxel(world, x - 1, y, z + 1),
                    wn = getVoxel(world, x - 1, y, z - 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y, z,
            getLight(voxels, light, sunlight, wn, wb, getVoxel(world, x - 1, y - 1, z - 1)),
            x, y, z + 1,
            getLight(voxels, light, sunlight, ws, wb, getVoxel(world, x - 1, y - 1, z + 1)),
            x, y + 1, z + 1,
            getLight(voxels, light, sunlight, ws, wt, getVoxel(world, x - 1, y + 1, z + 1)),
            x, y + 1, z,
            getLight(voxels, light, sunlight, wn, wt, getVoxel(world, x - 1, y + 1, z - 1))
          );
        }
      }
    }
  }
  bounds[0] = 0.5f * (box[0] + box[3]);
  bounds[1] = 0.5f * (box[1] + box[4]);
  bounds[2] = 0.5f * (box[2] + box[5]);
  const float halfWidth = 0.5f * (box[3] - box[0]),
              halfHeight = 0.5f * (box[4] - box[1]),
              halfDepth = 0.5f * (box[5] - box[2]);
  bounds[3] = sqrt(
    halfWidth * halfWidth
    + halfHeight * halfHeight
    + halfDepth * halfDepth
  );
  return faces;
}
