// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/Brush.h>
#include <noggit/Log.h>
#include <noggit/MapHeaders.h>
#include <noggit/MapTile.h>
#include <noggit/Misc.h>
#include <noggit/TextureManager.h> // TextureManager, Texture
#include <noggit/World.h>
#include <noggit/texture_set.hpp>

#include <algorithm>    // std::min
#include <iostream>     // std::cout

#include <boost/utility/in_place_factory.hpp>

void TextureSet::initTextures(MPQFile* f, MapTile* maintile, uint32_t size)
{
  // texture info
  nTextures = size / 16U;

  for (size_t i = 0; i<nTextures; ++i) 
  {
    f->read(&tex[i], 4);
    f->read(&texFlags[i], 4);
    f->read(&MCALoffset[i], 4);
    f->read(&effectID[i], 4);
    textures.emplace_back (maintile->mTextureFilenames[tex[i]]);
  }
}

void TextureSet::initAlphamaps(MPQFile* f, size_t nLayers, bool mBigAlpha, bool doNotFixAlpha)
{
  unsigned int MCALbase = f->getPos();

  for (unsigned int layer = 0; layer < nLayers; ++layer)
  {
    if (texFlags[layer] & 0x100)
    {
      f->seek(MCALbase + MCALoffset[layer]);
      alphamaps[layer - 1] = boost::in_place (f, texFlags[layer], mBigAlpha, doNotFixAlpha);
    }
  }

  // convert big alphas to the old format to be rendered correctly in noggit
  if (mBigAlpha)
  {
    convertToOldAlpha();
  }
}

int TextureSet::addTexture(scoped_blp_texture_reference texture)
{
  int texLevel = -1;

  if (nTextures < 4U)
  {
    texLevel = nTextures;
    nTextures++;

    textures.emplace_back (texture);
    texFlags[texLevel] = 0;
    effectID[texLevel] = 0;

    if (texLevel)
    {
      alphamaps[texLevel - 1] = boost::in_place();
    }
  }

  return texLevel;
}

void TextureSet::switchTexture (scoped_blp_texture_reference oldTexture, scoped_blp_texture_reference newTexture)
{
  int texLevel = -1;
  for (size_t i = 0; i < nTextures; ++i)
  {
    if (textures[i] == oldTexture)
      texLevel = i;
    // prevent texture duplication
    if (textures[i] == newTexture)
      return;
  }

  if (texLevel != -1)
  {
    textures[texLevel] = newTexture;
  }
}

// swap 2 textures of a chunk with their alpha
void TextureSet::swapTexture(int id1, int id2)
{
  if (id1 >= 0 && id2 >= 0 && id1 < nTextures && id2 < nTextures)
  {
    scoped_blp_texture_reference temp = textures[id1];
    textures[id1] = textures[id2];
    textures[id2] = temp;

    for (int j = 0; j < 64; j++)
    {
      for (int i = 0; i < 64; ++i)
      {
        float alphas[3] = { 0.0f, 0.0f, 0.0f };
        float visibility[4] = { 255.0f, 0.0f, 0.0f, 0.0f };

        for (size_t k = 0; k < nTextures - 1; k++)
        {
          float f = static_cast<float>(alphamaps[k]->getAlpha(i + j * 64));
          visibility[k + 1] = f;
          alphas[k] = f;
          for (size_t n = 0; n <= k; n++)
            visibility[n] = (visibility[n] * ((255.0f - f)) / 255.0f);
        }

        float tmp = visibility[id1];
        visibility[id1] = visibility[id2];
        visibility[id2] = tmp;

        for (int k = nTextures - 2; k >= 0; k--)
        {
          alphas[k] = visibility[k + 1];
          for (int n = nTextures - 2; n > k; n--)
          {
            // prevent 0 division
            if (alphas[n] == 255.0f)
            {
              alphas[k] = 0.0f;
              break;
            }
            else
              alphas[k] = (alphas[k] / (255.0f - alphas[n])) * 255.0f;
          }
        }

        for (size_t k = 0; k < nTextures - 1; k++)
        {
          alphamaps[k]->setAlpha(i + j * 64, static_cast<unsigned char>(std::min(std::max(alphas[k], 0.0f), 255.0f)));
          alphamaps[k]->loadTexture();
        }
      }
    }
  }
}

void TextureSet::eraseTextures()
{
  for (size_t i = nTextures-1; nTextures; --i)
  {
    eraseTexture(i);
  }
}

void TextureSet::eraseTexture(size_t id)
{
  if (id > 3)
    return;

  // shift textures above
  for (size_t i = id; i < nTextures - 1; i++)
  {
    if (i)
    {
      alphamaps[id - 1] = boost::none;
      std::swap (alphamaps[i - 1], alphamaps[i]);
    }

    textures[i] = textures[i + 1];
    texFlags[i] = texFlags[i + 1];
    effectID[i] = effectID[i + 1];
  }

  alphamaps[nTextures - 2] = boost::none;
  textures.pop_back();

  nTextures--;
}

bool TextureSet::canPaintTexture(scoped_blp_texture_reference texture)
{
  if (nTextures)
  {
    for (size_t k = 0; k < nTextures; ++k)
    {
      if (textures[k] == texture)
      {
        return true;
      }
    }

    return nTextures < 4;
  }

  return false;
}

const std::string& TextureSet::filename(size_t id)
{
  return textures[id]->filename();
}

void TextureSet::bindAlphamap(size_t id, size_t activeTexture)
{
  opengl::texture::enable_texture (activeTexture);

  alphamaps[id]->bind();
}

void TextureSet::bindTexture(size_t id, size_t activeTexture)
{
  opengl::texture::enable_texture (activeTexture);

  textures[id]->bind();
}

void TextureSet::startAnim(int id, int animtime)
{
  if (is_animated(id))
  {
    opengl::texture::set_active_texture (0);
    gl.matrixMode(GL_TEXTURE);
    gl.pushMatrix();

    
    const int spd = (texFlags[id] >> 3) & 0x7;
    const int dir = texFlags[id] & 0x7;
    const float texanimxtab[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    const float texanimytab[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    const float fdx = -texanimxtab[dir], fdy = texanimytab[dir];
    const int animspd = (const int)(200 * detail_size);
    float f = ((static_cast<int>(animtime*(spd / 7.0f))) % animspd) / static_cast<float>(animspd);
    gl.translatef(f*fdx, f*fdy, 0);
  }
}

void TextureSet::stopAnim(int id)
{
  if (is_animated(id))
  {
    gl.popMatrix();
    gl.matrixMode(GL_MODELVIEW);
    opengl::texture::set_active_texture (1);
  }
}

bool TextureSet::eraseUnusedTextures()
{
  bool texRemoved = false;

  if (nTextures < 2)
    return texRemoved;

  unsigned char alpha[64 * 64];
  bool baseVisible = false;
  size_t texCount = nTextures;

  for (size_t k = nTextures - 1; k > 0; k--)
  {
    bool texVisible = false;
    // use a temp variable because nTexture can be decreased at the end of the loop
    // if the texture above is fully opaque, don't test textures bellow
    if (k >= texCount - 1 || baseVisible)
    {
      // reset baseVisible because this layer could be fully opaque
      baseVisible = false;
      memcpy(alpha, alphamaps[k - 1]->getAlpha(), 64 * 64);
      for (size_t i = 0; i < 64 * 64; i++)
      {
        unsigned char a = alpha[i];
        if (a > 0)
        {
          texVisible = true;

          if (a < 255)
          {
            baseVisible = true;
            break;
          }
        }
        else
        {
          baseVisible = true;
        }
      }
    }

    if (!texVisible)
    {
      eraseTexture(k);
      texRemoved = true;
    }
  }

  // there will always be at least 2 textures when entering the condition
  if (!baseVisible)
  {
    // swap the base layer with the layer above
    swapTexture(0, 1);
    eraseTexture(1);
    texRemoved = true;
  }

  return texRemoved;
}

bool TextureSet::paintTexture(float xbase, float zbase, float x, float z, Brush* brush, float strength, float pressure, scoped_blp_texture_reference texture)
{
  bool changed = false;

  float zPos, xPos, dist, radius;

  // hacky fix to make sure textures are blended between 2 chunks
  if (z < zbase)
  {
    zbase -= TEXDETAILSIZE;
  }
  else if (z > zbase + CHUNKSIZE)
  {
    zbase += TEXDETAILSIZE;
  }

  if (x < xbase)
  {
    xbase -= TEXDETAILSIZE;
  }
  else if (x > xbase + CHUNKSIZE)
  {
    xbase += TEXDETAILSIZE;
  }

  //xbase, zbase mapchunk pos
  //x, y mouse pos

  int texLevel = -1;
  radius = brush->getRadius();
  dist = misc::getShortestDist(x, z, xbase, zbase, CHUNKSIZE);

  if (dist > radius)
    return changed;

  //First Lets find out do we have the texture already
  for (size_t i = 0; i<nTextures; ++i)
    if (textures[i] == texture)
      texLevel = i;

  if (texLevel == -1 && strength == 0)
  {
    return false;
  }

  if ((texLevel == -1) && (nTextures == 4) && !eraseUnusedTextures())
  {
    LogDebug << "paintTexture: No free texture slot" << std::endl;
    return false;
  }

  //Only 1 layer and its that layer
  if ((texLevel != -1) && (nTextures == 1))
    return true;

  if (texLevel == -1)
  {
    texLevel = addTexture(texture);
    if (texLevel == 0)
      return true;
    if (texLevel == -1)
    {
      LogDebug << "paintTexture: Unable to add texture." << std::endl;
      return false;
    }
  }

  zPos = zbase;
  bool texVisible[4] = { false, false, false, false };

  for (int j = 0; j < 64; j++)
  {
    xPos = xbase;
    for (int i = 0; i < 64; ++i)
    {
      dist = misc::dist(x, z, xPos + TEXDETAILSIZE / 2.0f, zPos + TEXDETAILSIZE / 2.0f);

      if (dist>radius)
      {
        bool baseVisible = true;
        for (size_t k = nTextures - 1; k > 0; k--)
        {
          unsigned char a = alphamaps[k - 1]->getAlpha(i + j * 64);

          if (a > 0)
          {
            texVisible[k] = true;

            if (a == 255)
            {
              baseVisible = false;
            }
          }
        }
        texVisible[0] = texVisible[0] || baseVisible;

        xPos += TEXDETAILSIZE;
        continue;
      }

      float tPressure = pressure*brush->getValue(dist);
      float alphas[3] = { 0.0f, 0.0f, 0.0f };
      float visibility[4] = { 255.0f, 0.0f, 0.0f, 0.0f };

      for (size_t k = 0; k < nTextures - 1; k++)
      {
        float f = static_cast<float>(alphamaps[k]->getAlpha(i + j * 64));
        visibility[k+1] = f;
        alphas[k] = f;
        for (size_t n = 0; n <= k; n++)
          visibility[n] = (visibility[n] * ((255.0f - f)) / 255.0f);
      }

      // nothing to do
      if (visibility[texLevel] == strength)
      {
        for (size_t k = 0; k < nTextures; k++)
        {
          texVisible[k] = texVisible[k] || (visibility[k] > 0.0f);
        }

        xPos += TEXDETAILSIZE;
        continue;
      }

      // at this point we know for sure that the textures will be changed
      changed = true;

      // alpha delta
      float diffA = (strength - visibility[texLevel])* tPressure;

      // visibility = 255 => all other at 0
      if (visibility[texLevel] + diffA >= 255.0f)
      {
        for (size_t k = 0; k < nTextures; k++)
        {
          visibility[k] = (k == texLevel) ? 255.0f : 0.0f;
        }
      }
      else
      {
        float other = 255.0f - visibility[texLevel];

        if (visibility[texLevel] == 255.0f && diffA < 0.0f)
        {
          visibility[texLevel] += diffA;
          int idTex = (!texLevel) ? 1 : texLevel - 1; // nTexture > 1 else it'd have returned true at the beginning
          visibility[idTex] -= diffA;
        }
        else
        {
          visibility[texLevel] += diffA;

          for (size_t k = 0; k < nTextures; k++)
          {
            if (k == texLevel || visibility[k] == 0)
              continue;

            visibility[k] = visibility[k] - (diffA * (visibility[k] / other));
          }
        }
      }

      for (int k = nTextures - 2; k >= 0; k--)
      {
        alphas[k] = visibility[k+1];
        for (int n = nTextures - 2; n > k; n--)
        {
          // prevent 0 division
          if (alphas[n] == 255.0f)
          {
            alphas[k] = 0.0f;
            break;
          }
          else
            alphas[k] = (alphas[k] / (255.0f - alphas[n])) * 255.0f;
        }
      }

      for (size_t k = 0; k < nTextures; k++)
      {
        if (k < nTextures - 1)
        {
          alphamaps[k]->setAlpha(i + j * 64, static_cast<unsigned char>(std::min(std::max(std::round(alphas[k]), 0.0f), 255.0f)));
        }
        texVisible[k] = texVisible[k] || (visibility[k] > 0.0f);
      }

      xPos += TEXDETAILSIZE;
    }
    zPos += TEXDETAILSIZE;
  }

  if (!changed)
  {
    return false;
  }

  // stop after k=0 because k is unsigned
  for (size_t k = nTextures - 1; k < 4; k--)
  {
    if (!texVisible[k])
      eraseTexture(k);
  }

  if (nTextures < 2)
  {
    return changed;
  }

  for (size_t j = 0; j < nTextures - 1; j++)
  {
    if (j > 2)
    {
      LogError << "WTF how did you get here??? Get a cookie." << std::endl;
      continue;
    }

    alphamaps[j]->loadTexture();
  }

  return changed;
}

size_t TextureSet::num()
{
  return nTextures;
}

unsigned int TextureSet::flag(size_t id)
{
  return texFlags[id];
}

unsigned int TextureSet::effect(size_t id)
{
  return effectID[id];
}

bool TextureSet::is_animated(std::size_t id) const
{
  return (id < nTextures ? (texFlags[id] & FLAG_ANIMATE) : false);
}

void TextureSet::change_texture_flag(scoped_blp_texture_reference tex, std::size_t flag, bool add)
{
  int tex_level = -1;
  for (size_t i = 0; i < nTextures; ++i)
  {
    if (textures[i] == tex)
    {
      if (add)
      {
        // override the current speed/rotation
        if (flag & 0x3F)
        {
          texFlags[i] &= ~0x3F;
        }
        texFlags[i] |= flag;
      }
      else
      {
        texFlags[i] &= ~flag;
      }
      break;
    }
  }
}

void TextureSet::setAlpha(size_t id, size_t offset, unsigned char value)
{
  alphamaps[id]->setAlpha(offset, value);
}

void TextureSet::setAlpha(size_t id, unsigned char *amap)
{
  alphamaps[id]->setAlpha(amap);
}

unsigned char TextureSet::getAlpha(size_t id, size_t offset)
{
  return alphamaps[id]->getAlpha(offset);
}

const unsigned char *TextureSet::getAlpha(size_t id)
{
  return alphamaps[id]->getAlpha();
}

std::vector<std::vector<char>> TextureSet::get_compressed_alphamaps()
{
  std::vector<std::vector<char>> compressed;

  if (nTextures > 1)
  {
    unsigned char alpha[3 * 64 * 64];

    alphas_to_big_alpha(alpha);
    for (int i = 0; i < nTextures - 1; ++i)
    {
      compressed.emplace_back(get_compressed_alpha(i, alpha));
    }
  }

  return compressed;
}

std::vector<char> TextureSet::get_compressed_alpha(std::size_t id, unsigned char* alphas)
{
  struct entry
  {
    enum mode_t
    {
      copy = 0,              // append value[0..count - 1]
      fill = 1,              // append value[0] count times
    };    
    uint8_t count : 7;
    uint8_t mode : 1;
    
    uint8_t value[];
  };

  const unsigned char* alpha = alphas + 4096*id;
  std::vector<char> data(alpha, alpha+4096);
  auto current (data.begin());
  auto const end (data.end());
  int column_pos = 0;

  auto const consume_fill
  ( 
    [&]
    {
      int8_t count (0);
      column_pos %= 64;
      
      while ((current + 1 < end) && *current == *(current + 1) && column_pos < 63)
      {
        ++current;
        ++count;
        ++column_pos;
      }

      // include current (current is incremented in the for loop)
      if (count)
      {
        ++count;
        ++column_pos;
      }

      return count;
    }
  );

  std::vector<char> result;
  boost::optional<std::size_t> current_copy_entry_offset (boost::none);
  auto const current_copy_entry
  ( 
    [&]
    {
      return reinterpret_cast<entry*> (&*(result.begin() + *current_copy_entry_offset));
    }
  );

  for (; current != end; ++current)
  {
    auto const fill (consume_fill());
    if (fill)
    {
      current_copy_entry_offset = boost::none;

      result.emplace_back();
      result.emplace_back(*current);

      entry* e (reinterpret_cast<entry*> (&*(result.rbegin() + 1)));
      e->mode = entry::fill;
      e->count = fill;

      column_pos %= 64;
    }
    else
    {
      if ( current_copy_entry_offset == boost::none
        || column_pos == 64
         )
      {
        current_copy_entry_offset = result.size();
        result.emplace_back();
        result.emplace_back(*current);
        current_copy_entry()->mode = entry::copy;
        current_copy_entry()->count = 1;
        
        column_pos %= 64;
      }
      else
      {
        result.emplace_back(*current);
        current_copy_entry()->count++;
      }

      column_pos++;
    }
  }

  return result;
}

scoped_blp_texture_reference TextureSet::texture(size_t id)
{
  return textures[id];
}

// dest = tab [4096 * (nTextures - 1)]
// call only if nTextures > 1
void TextureSet::alphas_to_big_alpha(unsigned char* dest)
{
  auto alpha 
  ( 
    [&] (int layer, int pos = 0)
    {
      return dest + layer * 4096 + pos;
    }
  );

  for (size_t k = 0; k < nTextures - 1; k++)
  {
    memcpy(alpha(k), alphamaps[k]->getAlpha(), 64 * 64);
  }

  float alphas[3] = { 0.0f, 0.0f, 0.0f };

  for (int i = 0; i < 64 * 64; ++i)
  {
    for (size_t k = 0; k < nTextures - 1; k++)
    {
      float f = static_cast<float>(*alpha(k, i));
      alphas[k] = f;
      for (size_t n = 0; n < k; n++)
        alphas[n] = (alphas[n] * ((255.0f - f)) / 255.0f);
    }

    for (size_t k = 0; k < nTextures - 1; k++)
    {
      *alpha(k, i) = static_cast<unsigned char>(std::min(std::max(std::round(alphas[k]), 0.0f), 255.0f));
    }
  }
}

void TextureSet::convertToBigAlpha()
{
  // nothing to do
  if (nTextures < 2)
    return;

  unsigned char tab[64 * 64 * 3];

  alphas_to_big_alpha(tab);

  for (size_t k = 0; k < nTextures - 1; k++)
  {
    alphamaps[k]->setAlpha(tab + 4096 * k);
    alphamaps[k]->loadTexture();
  }
}

void TextureSet::convertToOldAlpha()
{
  // nothing to do
  if (nTextures < 2)
    return;

  unsigned char tab[3][64 * 64];

  for (size_t k = 0; k < nTextures - 1; k++)
  {
    memcpy(tab[k], alphamaps[k]->getAlpha(), 64 * 64);
  }

  float alphas[3] = { 0.0f, 0.0f, 0.0f };

  for (int i = 0; i < 64 * 64; ++i)
  {
    for (size_t k = 0; k < nTextures - 1; k++)
    {
      alphas[k] = static_cast<float>(tab[k][i]);
    }

    for (int k = nTextures - 2; k >= 0; k--)
    {
      for (int n = nTextures - 2; n > k; n--)
      {
        // prevent 0 division
        if (alphas[n] == 255.0f)
        {
          alphas[k] = 0.0f;
          break;
        }
        else
          alphas[k] = (alphas[k] / (255.0f - alphas[n])) * 255.0f;
      }
    }

    for (size_t k = 0; k < nTextures - 1; k++)
    {
      tab[k][i] = static_cast<unsigned char>(std::min(std::max(std::round(alphas[k]), 0.0f), 255.0f));
    }
  }

  for (size_t k = 0; k < nTextures - 1; k++)
  {
    alphamaps[k]->setAlpha(tab[k]);
    alphamaps[k]->loadTexture();
  }
}

void TextureSet::mergeAlpha(size_t id1, size_t id2)
{
  if (id1 >= nTextures || id2 >= nTextures || id1 == id2)
    return;

  unsigned char tab[3][64 * 64];

  for (size_t k = 0; k < nTextures - 1; k++)
  {
    memcpy(tab[k], alphamaps[k]->getAlpha(), 64 * 64);
  }

  float alphas[3] = { 0.0f, 0.0f, 0.0f };
  float visibility[4] = { 255.0f, 0.0f, 0.0f, 0.0f };

  for (int i = 0; i < 64 * 64; ++i)
  {
    for (size_t k = 0; k < nTextures - 1; k++)
    {
      float f = static_cast<float>(tab[k][i]);
      visibility[k + 1] = f;
      for (size_t n = 0; n <= k; n++)
        visibility[n] = (visibility[n] * ((255.0f - f)) / 255.0f);
    }

    visibility[id1] += visibility[id2];
    visibility[id2] = 0;

    for (int k = nTextures - 2; k >= 0; k--)
    {
      alphas[k] = visibility[k + 1];
      for (int n = nTextures - 2; n > k; n--)
      {
        // prevent 0 division
        if (alphas[n] == 255.0f)
        {
          alphas[k] = 0.0f;
          break;
        }
        else
          alphas[k] = (alphas[k] / (255.0f - alphas[n])) * 255.0f;
      }
    }

    for (size_t k = 0; k < nTextures - 1; k++)
    {
      tab[k][i] = static_cast<unsigned char>(std::min(std::max(std::round(alphas[k]), 0.0f), 255.0f));
    }
  }

  for (size_t k = 0; k < nTextures - 1; k++)
  {
    alphamaps[k]->setAlpha(tab[k]);
    alphamaps[k]->loadTexture();
  }

  eraseTexture(id2);
}

bool TextureSet::removeDuplicate()
{
  bool changed = false;

  for (size_t i = 0; i < nTextures; i++)
  {
    for (size_t j = i + 1; j < nTextures; j++)
    {
      if (textures[i] == textures[j])
      {
        mergeAlpha(i, j);
        changed = true;
      }
    }
  }

  return changed;
}
