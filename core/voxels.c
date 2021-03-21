#define FNL_IMPL
#include "../vendor/FastNoiseLite.h"

enum {
  VOXEL_TYPE,
  VOXEL_R,
  VOXEL_G,
  VOXEL_B,
  VOXEL_LIGHT,
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
  const World *world,
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

static const float getLight(
  unsigned char *voxels,
  unsigned char light,
  const int n1,
  const int n2,
  const int n3
) {
  float ao = 0.0f;
  {
    const unsigned char v1 = (n1 != -1 && voxels[n1] != 0) ? 1 : 0;
    const unsigned char v2 = (n2 != -1 && voxels[n2] != 0) ? 1 : 0;
    const unsigned char v3 = (n3 != -1 && voxels[n3] != 0) ? 1 : 0;
    if (v1 == 1) ao += 0.1f;
    if (v2 == 1) ao += 0.1f;
    if ((v1 == 1 && v2 == 1) || v3 == 1) ao += 0.1f;
  }

  float sunlight = light;
  {
    unsigned char n = 1;
    const unsigned char v1 = (n1 != -1 && voxels[n1] == 0) ? 1 : 0;
    const unsigned char v2 = (n2 != -1 && voxels[n2] == 0) ? 1 : 0;
    const unsigned char v3 = (n3 != -1 && voxels[n3] == 0) ? 1 : 0;
    if (v1 == 1) {
      sunlight += voxels[n1 + VOXEL_LIGHT];
      n++;
    }
    if (v2 == 1) {
      sunlight += voxels[n2 + VOXEL_LIGHT];
      n++;
    }
    if ((v1 == 1 || v2 == 1) && v3 == 1) {
      sunlight += voxels[n3 + VOXEL_LIGHT];
      n++;
    }
    sunlight = sunlight / n / maxLight;
  }

  return _fnlFastMax(sunlight * sunlight, 0.1f) * (1.0f - ao);
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

static void growBox(
  int *box,
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
  int *box,
  unsigned int *faces,
  unsigned int *indices,
  unsigned char *vertices,
  const int chunkX, const int chunkY, const int chunkZ,
  const unsigned char r, const unsigned char g, const unsigned char b,
  unsigned char x1, unsigned char y1, unsigned char z1, const float l1,
  unsigned char x2, unsigned char y2, unsigned char z2, const float l2,
  unsigned char x3, unsigned char y3, unsigned char z3, const float l3,
  unsigned char x4, unsigned char y4, unsigned char z4, const float l4
) {
  const unsigned int vertex = *faces * 4;
  const unsigned int vertexOffset = vertex * 6;
  const unsigned int indexOffset = *faces * 6;
  const unsigned int flipFace = l1 + l3 < l2 + l4 ? 1 : 0; // Fixes interpolation anisotropy
  (*faces)++;
  x1 -= chunkX;
  y1 -= chunkY;
  z1 -= chunkZ;
  growBox(box, x1, y1, z1);
  x2 -= chunkX;
  y2 -= chunkY;
  z2 -= chunkZ;
  growBox(box, x2, y2, z2);
  x3 -= chunkX;
  y3 -= chunkY;
  z3 -= chunkZ;
  growBox(box, x3, y3, z3);
  x4 -= chunkX;
  y4 -= chunkY;
  z4 -= chunkZ;
  growBox(box, x4, y4, z4);
  // Is this crazy? I dunno. You tell me.
  vertices[vertexOffset] = x1;
  vertices[vertexOffset + 1] = y1;
  vertices[vertexOffset + 2] = z1;
  vertices[vertexOffset + 3] = r * l1;
  vertices[vertexOffset + 4] = g * l1;
  vertices[vertexOffset + 5] = b * l1;
  vertices[vertexOffset + 6] = x2;
  vertices[vertexOffset + 7] = y2;
  vertices[vertexOffset + 8] = z2;
  vertices[vertexOffset + 9] = r * l2;
  vertices[vertexOffset + 10] = g * l2;
  vertices[vertexOffset + 11] = b * l2;
  vertices[vertexOffset + 12] = x3;
  vertices[vertexOffset + 13] = y3;
  vertices[vertexOffset + 14] = z3;
  vertices[vertexOffset + 15] = r * l3;
  vertices[vertexOffset + 16] = g * l3;
  vertices[vertexOffset + 17] = b * l3;
  vertices[vertexOffset + 18] = x4;
  vertices[vertexOffset + 19] = y4;
  vertices[vertexOffset + 20] = z4;
  vertices[vertexOffset + 21] = r * l4;
  vertices[vertexOffset + 22] = g * l4;
  vertices[vertexOffset + 23] = b * l4;
  indices[indexOffset] = vertex + flipFace;
  indices[indexOffset + 1] = vertex + flipFace + 1;
  indices[indexOffset + 2] = vertex + flipFace + 2;
  indices[indexOffset + 3] = vertex + flipFace + 2;
  indices[indexOffset + 4] = vertex + ((flipFace + 3) % 4);
  indices[indexOffset + 5] = vertex + flipFace;
}

void floodLight(
  const World *world,
  int *heightmap,
  unsigned char *voxels,
  int *queue,
  const int size,
  int *next
) {
  unsigned int nextLength = 0;
  for (unsigned int i = 0; i < size; i++) {
    const int voxel = queue[i];
    const unsigned char light = voxels[voxel + VOXEL_LIGHT];
    if (light == 0) {
      continue;
    }
    const int index = voxel / 5;
    const int z = _fnlFastFloor(index / (world->width * world->height));
    const int y = _fnlFastFloor((index % (world->width * world->height)) / world->width);
    const int x = _fnlFastFloor((index % (world->width * world->height)) % world->width);
    for (unsigned char n = 0; n < 6; n += 1) {
      const int nx = x + neighbors[n * 3];
      const int ny = y + neighbors[n * 3 + 1];
      const int nz = z + neighbors[n * 3 + 2];
      const int neighbor = getVoxel(world, nx, ny, nz);
      const unsigned char nl = n == 5 && light == maxLight ? light : light - 1;
      if (
        neighbor == -1
        || voxels[neighbor] != 0
        || (
          n != 5
          && light == maxLight
          && ny > heightmap[(nz * world->width) + nx]
        )
        || voxels[neighbor + VOXEL_LIGHT] >= nl
      ) {
        continue;
      }
      voxels[neighbor + VOXEL_LIGHT] = nl;
      next[nextLength++] = neighbor;
    }
  }
  if (nextLength > 0) {
    floodLight(
      world,
      heightmap,
      voxels,
      next,
      nextLength,
      queue
    );
  }
}

void removeLight(
  const World *world,
  int *heightmap,
  unsigned char *voxels,
  int *queue,
  const int size,
  int *next,
  int *floodQueue,
  int floodQueueSize,
  int *floodNext
) {
  int nextLength = 0;
  for (int i = 0; i < size; i += 2) {
    const int voxel = queue[i];
    const unsigned char light = queue[i + 1];
    const int index = voxel / 5;
    const int z = _fnlFastFloor(index / (world->width * world->height));
    const int y = _fnlFastFloor((index % (world->width * world->height)) / world->width);
    const int x = _fnlFastFloor((index % (world->width * world->height)) % world->width);
    for (unsigned char n = 0; n < 6; n += 1) {
      const int neighbor = getVoxel(
        world,
        x + neighbors[n * 3],
        y + neighbors[n * 3 + 1],
        z + neighbors[n * 3 + 2]
      );
      if (
        neighbor == -1
        || voxels[neighbor] != 0
        || voxels[neighbor + VOXEL_LIGHT] == 0
      ) {
        continue;
      }
      const unsigned char nl = voxels[neighbor + VOXEL_LIGHT];
      if (
        nl < light
        || (
          n == 5
          && light == maxLight
          && nl == maxLight
        )
      ) {
        next[nextLength++] = neighbor;
        next[nextLength++] = nl;
        voxels[neighbor + VOXEL_LIGHT] = 0;
      } else if (nl >= light) {
        floodQueue[floodQueueSize++] = neighbor;
      }
    }
  }
  if (nextLength > 0) {
    removeLight(
      world,
      heightmap,
      voxels,
      next,
      nextLength,
      queue,
      floodQueue,
      floodQueueSize,
      floodNext
    );
  } else if (floodQueueSize > 0) {
    floodLight(
      world,
      heightmap,
      voxels,
      floodQueue,
      floodQueueSize,
      floodNext
    );
  }
}

void generate(
  const World *world,
  int *heightmap,
  unsigned char *voxels,
  const int seed
) {
  fnl_state noise = fnlCreateState();
  noise.seed = seed;
  noise.fractal_type = FNL_FRACTAL_FBM;
  for (int z = 32; z < world->depth - 32; z++) {
    for (int y = 0; y < world->height; y++) {
      for (int x = 32; x < world->width - 32; x++) {
        const float n = _fnlFastAbs(fnlGetNoise3D(&noise, x, y, z));
        const int h = n * world->height;
        if (y <= h) {
          const int voxel = getVoxel(world, x, y, z);
          const unsigned int color = getColorFromNoise(0xFF * n);
          voxels[voxel] = 0x01;
          voxels[voxel + VOXEL_R] = (color >> 16) & 0xFF;
          voxels[voxel + VOXEL_G] = (color >> 8) & 0xFF;
          voxels[voxel + VOXEL_B] = color & 0xFF;
          const int heightmapIndex = z * world->width + x;
          if (heightmap[heightmapIndex] < y) {
            heightmap[heightmapIndex] = y;
          }
        }
      }
    }
  }
}

void propagate(
  const World *world,
  int *heightmap,
  unsigned char *voxels,
  int *queueA,
  int *queueB
) {
  int queueSize = 0;
  for (int z = 0, voxel = 0; z < world->depth; z++) {
    for (int y = 0; y < world->height; y++) {
      for (int x = 0; x < world->width; x++, voxel += 5) {
        if (y == (world->height - 1) && voxels[voxel] == 0) {
          voxels[voxel + VOXEL_LIGHT] = maxLight;
          queueA[queueSize++] = voxel;
        } else {
          voxels[voxel + VOXEL_LIGHT] = 0;
        }
      }
    }
  }
  floodLight(
    world,
    heightmap,
    voxels,
    queueA,
    queueSize,
    queueB
  );
}

const int mesh(
  const World *world,
  const int chunkX,
  const int chunkY,
  const int chunkZ,
  const int chunkSize,
  unsigned char *voxels,
  float *bounds,
  unsigned int *indices,
  unsigned char *vertices
) {
  if (
    chunkX + chunkSize > world->width
    || chunkY + chunkSize > world->height
    || chunkZ + chunkSize > world->depth
  ) {
    return -1;
  }
  // WELCOME TO THE JUNGLE !!
  int box[6] = { chunkSize, chunkSize, chunkSize, 0, 0, 0 };
  unsigned int faces = 0;
  for (int z = chunkZ; z < chunkZ + chunkSize; z++) {
    for (int y = chunkY; y < chunkY + chunkSize; y++) {
      for (int x = chunkX; x < chunkX + chunkSize; x++) {
        const int voxel = getVoxel(world, x, y, z);
        if (voxels[voxel] == 0) {
          continue;
        }
        const unsigned char r = voxels[voxel + VOXEL_R];
        const unsigned char g = voxels[voxel + VOXEL_G];
        const unsigned char b = voxels[voxel + VOXEL_B];
        const int top = getVoxel(world, x, y + 1, z);
        const int bottom = getVoxel(world, x, y - 1, z);
        const int south = getVoxel(world, x, y, z + 1);
        const int north = getVoxel(world, x, y, z - 1);
        const int east = getVoxel(world, x + 1, y, z);
        const int west = getVoxel(world, x - 1, y, z);
        if (top != -1 && voxels[top] == 0) {
          const unsigned char light = voxels[top + VOXEL_LIGHT];
          const int ts = getVoxel(world, x, y + 1, z + 1);
          const int tn = getVoxel(world, x, y + 1, z - 1);
          const int te = getVoxel(world, x + 1, y + 1, z);
          const int tw = getVoxel(world, x - 1, y + 1, z);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y + 1, z + 1,
            getLight(voxels, light, tw, ts, getVoxel(world, x - 1, y + 1, z + 1)),
            x + 1, y + 1, z + 1,
            getLight(voxels, light, te, ts, getVoxel(world, x + 1, y + 1, z + 1)),
            x + 1, y + 1, z,
            getLight(voxels, light, te, tn, getVoxel(world, x + 1, y + 1, z - 1)),
            x, y + 1, z,
            getLight(voxels, light, tw, tn, getVoxel(world, x - 1, y + 1, z - 1))
          );
        }
        if (bottom != -1 && voxels[bottom] == 0) {
          const unsigned char light = voxels[bottom + VOXEL_LIGHT];
          const int bs = getVoxel(world, x, y - 1, z + 1);
          const int bn = getVoxel(world, x, y - 1, z - 1);
          const int be = getVoxel(world, x + 1, y - 1, z);
          const int bw = getVoxel(world, x - 1, y - 1, z);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y, z,
            getLight(voxels, light, bw, bn, getVoxel(world, x - 1, y - 1, z - 1)),
            x + 1, y, z,
            getLight(voxels, light, be, bn, getVoxel(world, x + 1, y - 1, z - 1)),
            x + 1, y, z + 1,
            getLight(voxels, light, be, bs, getVoxel(world, x + 1, y - 1, z + 1)),
            x, y, z + 1,
            getLight(voxels, light, bw, bs, getVoxel(world, x - 1, y - 1, z + 1))
          );
        }
        if (south != -1 && voxels[south] == 0) {
          const unsigned char light = voxels[south + VOXEL_LIGHT];
          const int st = getVoxel(world, x, y + 1, z + 1);
          const int sb = getVoxel(world, x, y - 1, z + 1);
          const int se = getVoxel(world, x + 1, y, z + 1);
          const int sw = getVoxel(world, x - 1, y, z + 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y, z + 1,
            getLight(voxels, light, sw, sb, getVoxel(world, x - 1, y - 1, z + 1)),
            x + 1, y, z + 1,
            getLight(voxels, light, se, sb, getVoxel(world, x + 1, y - 1, z + 1)),
            x + 1, y + 1, z + 1,
            getLight(voxels, light, se, st, getVoxel(world, x + 1, y + 1, z + 1)),
            x, y + 1, z + 1,
            getLight(voxels, light, sw, st, getVoxel(world, x - 1, y + 1, z + 1))
          );
        }
        if (north != -1 && voxels[north] == 0) {
          const unsigned char light = voxels[north + VOXEL_LIGHT];
          const int nt = getVoxel(world, x, y + 1, z - 1);
          const int nb = getVoxel(world, x, y - 1, z - 1);
          const int ne = getVoxel(world, x + 1, y, z - 1);
          const int nw = getVoxel(world, x - 1, y, z - 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x + 1, y, z,
            getLight(voxels, light, ne, nb, getVoxel(world, x + 1, y - 1, z - 1)),
            x, y, z,
            getLight(voxels, light, nw, nb, getVoxel(world, x - 1, y - 1, z - 1)),
            x, y + 1, z,
            getLight(voxels, light, nw, nt, getVoxel(world, x - 1, y + 1, z - 1)),
            x + 1, y + 1, z,
            getLight(voxels, light, ne, nt, getVoxel(world, x + 1, y + 1, z - 1))
          );
        }
        if (east != -1 && voxels[east] == 0) {
          const unsigned char light = voxels[east + VOXEL_LIGHT];
          const int et = getVoxel(world, x + 1, y + 1, z);
          const int eb = getVoxel(world, x + 1, y - 1, z);
          const int es = getVoxel(world, x + 1, y, z + 1);
          const int en = getVoxel(world, x + 1, y, z - 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x + 1, y, z + 1,
            getLight(voxels, light, es, eb, getVoxel(world, x + 1, y - 1, z + 1)),
            x + 1, y, z,
            getLight(voxels, light, en, eb, getVoxel(world, x + 1, y - 1, z - 1)),
            x + 1, y + 1, z,
            getLight(voxels, light, en, et, getVoxel(world, x + 1, y + 1, z - 1)),
            x + 1, y + 1, z + 1,
            getLight(voxels, light, es, et, getVoxel(world, x + 1, y + 1, z + 1))
          );
        }
        if (west != -1 && voxels[west] == 0) {
          const unsigned char light = voxels[west + VOXEL_LIGHT];
          const int wt = getVoxel(world, x - 1, y + 1, z);
          const int wb = getVoxel(world, x - 1, y - 1, z);
          const int ws = getVoxel(world, x - 1, y, z + 1);
          const int wn = getVoxel(world, x - 1, y, z - 1);
          pushFace(
            box,
            &faces,
            indices,
            vertices,
            chunkX, chunkY, chunkZ,
            r, g, b,
            x, y, z,
            getLight(voxels, light, wn, wb, getVoxel(world, x - 1, y - 1, z - 1)),
            x, y, z + 1,
            getLight(voxels, light, ws, wb, getVoxel(world, x - 1, y - 1, z + 1)),
            x, y + 1, z + 1,
            getLight(voxels, light, ws, wt, getVoxel(world, x - 1, y + 1, z + 1)),
            x, y + 1, z,
            getLight(voxels, light, wn, wt, getVoxel(world, x - 1, y + 1, z - 1))
          );
        }
      }
    }
  }
  bounds[0] = (box[0] + box[3]) * 0.5f;
  bounds[1] = (box[1] + box[4]) * 0.5f;
  bounds[2] = (box[2] + box[5]) * 0.5f;
  const float halfWidth = 0.5f * (box[3] - box[0]);
  const float halfHeight = 0.5f * (box[4] - box[1]);
  const float halfDepth = 0.5f * (box[5] - box[2]);
  bounds[3] = sqrt(
    halfWidth * halfWidth
    + halfHeight * halfHeight
    + halfDepth * halfDepth
  );
  return faces;
}

void simulate(
  const World *world,
  int *heightmap,
  unsigned char *voxels
) {
  // This is mainly just for testing
  // Doing animations like this requires full repropagation
  // So... it's not truly feasible unless the volume is really small
  for (int y = 1; y < world->height; y++) {
    for (int z = 2; z < world->depth - 2; z++) {
      for (int x = 2; x < world->width - 2; x++) {
        const int voxel = getVoxel(world, x, y, z);
        if (voxels[voxel] == 0) {
          continue;
        }
        // Drop everything to the ground
        int neighbor = getVoxel(world, x, y - 1, z);
        if (neighbor == -1 || voxels[neighbor] != 0) {
          neighbor = getVoxel(world, x - 1, y - 1, z);
          if (neighbor == -1 || voxels[neighbor] != 0) {
            neighbor = getVoxel(world, x, y - 1, z + 1);
            if (neighbor == -1 || voxels[neighbor] != 0) {
              neighbor = getVoxel(world, x, y - 1, z - 1);
              if (neighbor == -1 || voxels[neighbor] != 0) {
                neighbor = getVoxel(world, x + 1, y - 1, z);
                if (neighbor == -1 || voxels[neighbor] != 0) {
                  continue;
                }
              }
            }
          }
        }
        voxels[neighbor] = voxels[voxel];
        voxels[neighbor + VOXEL_R] = voxels[voxel + VOXEL_R];
        voxels[neighbor + VOXEL_G] = voxels[voxel + VOXEL_G];
        voxels[neighbor + VOXEL_B] = voxels[voxel + VOXEL_B];
        voxels[voxel] = 0;
        voxels[voxel + VOXEL_R] = 0;
        voxels[voxel + VOXEL_G] = 0;
        voxels[voxel + VOXEL_B] = 0;
      }
    }
   }
}
