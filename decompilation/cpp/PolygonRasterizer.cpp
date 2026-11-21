#include "PolygonRasterizer.h"
#include <bit>
// This translation unit requires signed wrap semantics (-fwrapv), matching
// the original 32-bit IMUL/ADD/SUB instructions. Mapped fields use byte access
// rather than casting the backing store to aliased or unaligned host ints.
namespace trilogy::original {
static_assert(sizeof(int)==4 && sizeof(unsigned)==4);
namespace {
using Mem=PlayerControlMemory;
int si(unsigned n){return std::bit_cast<std::int32_t>(n);}
struct SignedField {
    Mem m;unsigned address;
    operator int() const {return si(m.d(address));}
    int operator=(int value){m.d(address,static_cast<unsigned>(value));return value;}
    int operator=(const SignedField& value){return *this=static_cast<int>(value);}
};
}
void sortTriangleVertices(Mem m){
    unsigned xs[]={0x3f6de0,0x3f6de4,0x3f6de8},ys[]={0x3f6df0,0x3f6df4,0x3f6d9c},us[]={0x3f6dd0,0x3f6dd4,0x3f6dd8},vs[]={0x3f6d7c,0x3f6d80,0x3f6d84};
    auto swap=[&](unsigned a,unsigned b){for(auto addresses:{xs,ys,us,vs}){auto n=m.d(addresses[a]);m.d(addresses[a],m.d(addresses[b]));m.d(addresses[b],n);}};
    if(si(m.d(ys[1]))<si(m.d(ys[0])))swap(0,1);
    if(si(m.d(ys[2]))<si(m.d(ys[1])))swap(1,2);
    if(si(m.d(ys[1]))<si(m.d(ys[0])))swap(0,1);
}
void setupTexturedRectangle(Mem m,unsigned param_1,const RenderService& f)
{
  auto field_0009d504=SignedField{m,0x0009d504};
  auto field_0009d508=SignedField{m,0x0009d508};
  auto field_0009d544=SignedField{m,0x0009d544};
  auto field_003f6cf0=SignedField{m,0x003f6cf0};
  auto field_003f6d28=SignedField{m,0x003f6d28};
  auto field_003f6d30=SignedField{m,0x003f6d30};
  auto field_003f6d34=SignedField{m,0x003f6d34};
  auto field_003f6d44=SignedField{m,0x003f6d44};
  auto field_003f6d48=SignedField{m,0x003f6d48};
  auto field_003f6d4c=SignedField{m,0x003f6d4c};
  auto field_003f6d58=SignedField{m,0x003f6d58};
  auto field_003f6d68=SignedField{m,0x003f6d68};
  auto field_003f6d74=SignedField{m,0x003f6d74};
  auto field_003f6d78=SignedField{m,0x003f6d78};
  auto field_003f6d7c=SignedField{m,0x003f6d7c};
  auto field_003f6d84=SignedField{m,0x003f6d84};
  auto field_003f6d90=SignedField{m,0x003f6d90};
  auto field_003f6d94=SignedField{m,0x003f6d94};
  auto field_003f6d9c=SignedField{m,0x003f6d9c};
  auto field_003f6da8=SignedField{m,0x003f6da8};
  auto field_003f6dac=SignedField{m,0x003f6dac};
  auto field_003f6db0=SignedField{m,0x003f6db0};
  auto field_003f6dc4=SignedField{m,0x003f6dc4};
  auto field_003f6dc8=SignedField{m,0x003f6dc8};
  auto field_003f6dcc=SignedField{m,0x003f6dcc};
  auto field_003f6dd0=SignedField{m,0x003f6dd0};
  auto field_003f6dd4=SignedField{m,0x003f6dd4};
  auto field_003f6de0=SignedField{m,0x003f6de0};
  auto field_003f6de4=SignedField{m,0x003f6de4};
  auto field_003f6df0=SignedField{m,0x003f6df0};
  int iVar1;
  
  if ((((-1 < field_003f6d9c) && (field_003f6df0 <= field_0009d508)) && (field_003f6de0 <= field_0009d504)) &&
     ((-1 < field_003f6de4 && (field_003f6de0 != field_003f6de4)))) {
    field_003f6da8 = field_003f6dd0 * 0x10000;
    field_003f6dd4 = field_003f6dd4 * 0x10000;
    iVar1 = field_003f6d7c * 0x10000;
    field_003f6d84 = field_003f6d84 * 0x10000;
    field_003f6d94 = (field_003f6de4 - field_003f6de0) + -1;
    if (field_003f6d94 == 0) {
      field_003f6d94 = field_003f6de4 - field_003f6de0;
    }
    field_003f6d4c = (field_003f6dd4 + field_003f6dd0 * -0x10000) / field_003f6d94;
    field_003f6d48 = 0;
    field_003f6d94 = (field_003f6d9c - field_003f6df0) + -1;
    if (field_003f6d94 == 0) {
      field_003f6d94 = field_003f6d9c - field_003f6df0;
    }
    field_003f6dc4 = (field_003f6d84 + field_003f6d7c * -0x10000) / field_003f6d94;
    field_003f6dc8 = 0;
    field_003f6d44 = 0;
    field_003f6dcc = 0;
    field_003f6db0 = field_003f6de0 << 0x10;
    field_003f6d30 = 0;
    field_003f6d74 = 0;
    field_003f6d90 = field_003f6de4 << 0x10;
    field_003f6d34 = 0;
    field_003f6d68 = 0;
    field_003f6dac = iVar1;
    if (field_003f6df0 < 0) {
      field_003f6dac = iVar1 + -field_003f6df0 * field_003f6dc4;
      field_003f6df0 = 0;
    }
    field_003f6d58 = field_003f6d9c - field_003f6df0;
    field_003f6d7c = iVar1;
    field_003f6dd0 = field_003f6da8;
    if (((field_003f6d58 + field_003f6df0 <= field_0009d508) ||
        (field_003f6d58 = (field_0009d508 - field_003f6df0) + 1, -1 < field_003f6d58)) &&
       (field_003f6d58 != 0)) {
      if (field_0009d508 < field_003f6d58) {
        field_003f6d58 = field_0009d508;
      }
      field_003f6cf0 = field_0009d544 + field_003f6df0 * field_003f6d78;
      field_003f6d28 = param_1;
      f(m,m.d(0x9d510+static_cast<unsigned>(m.b(0x3f6e83))*4),{});
    }
  }
  return;
}

void setupTriangleSpans(Mem m,unsigned param_1,const RenderService& f)
{
  auto field_0009d508=SignedField{m,0x0009d508};
  auto field_0009d544=SignedField{m,0x0009d544};
  auto field_003f6cf0=SignedField{m,0x003f6cf0};
  auto field_003f6d28=SignedField{m,0x003f6d28};
  auto field_003f6d44=SignedField{m,0x003f6d44};
  auto field_003f6d48=SignedField{m,0x003f6d48};
  auto field_003f6d4c=SignedField{m,0x003f6d4c};
  auto field_003f6d50=SignedField{m,0x003f6d50};
  auto field_003f6d54=SignedField{m,0x003f6d54};
  auto field_003f6d58=SignedField{m,0x003f6d58};
  auto field_003f6d60=SignedField{m,0x003f6d60};
  auto field_003f6d68=SignedField{m,0x003f6d68};
  auto field_003f6d74=SignedField{m,0x003f6d74};
  auto field_003f6d78=SignedField{m,0x003f6d78};
  auto field_003f6d7c=SignedField{m,0x003f6d7c};
  auto field_003f6d80=SignedField{m,0x003f6d80};
  auto field_003f6d84=SignedField{m,0x003f6d84};
  auto field_003f6d90=SignedField{m,0x003f6d90};
  auto field_003f6d94=SignedField{m,0x003f6d94};
  auto field_003f6d98=SignedField{m,0x003f6d98};
  auto field_003f6d9c=SignedField{m,0x003f6d9c};
  auto field_003f6da8=SignedField{m,0x003f6da8};
  auto field_003f6dac=SignedField{m,0x003f6dac};
  auto field_003f6db0=SignedField{m,0x003f6db0};
  auto field_003f6dc4=SignedField{m,0x003f6dc4};
  auto field_003f6dc8=SignedField{m,0x003f6dc8};
  auto field_003f6dcc=SignedField{m,0x003f6dcc};
  auto field_003f6dd0=SignedField{m,0x003f6dd0};
  auto field_003f6dd4=SignedField{m,0x003f6dd4};
  auto field_003f6dd8=SignedField{m,0x003f6dd8};
  auto field_003f6de0=SignedField{m,0x003f6de0};
  auto field_003f6de4=SignedField{m,0x003f6de4};
  auto field_003f6de8=SignedField{m,0x003f6de8};
  auto field_003f6df0=SignedField{m,0x003f6df0};
  auto field_003f6df4=SignedField{m,0x003f6df4};
  unsigned uVar1;
  int iVar2;
  int iVar3;

  int iVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  int iVar8;
  int iVar9;
  int iVar10;
  
  if ((0x13f0000 < field_003f6de8 || (0x13f0000 < field_003f6de0 || 0x13f0000 < field_003f6de4)) &&
     (field_003f6de8 < 0 || (field_003f6de0 < 0 || field_003f6de4 < 0))) {
    return;
  }
  if ((field_003f6d9c < -0x40 || (field_003f6df0 < -0x40 || field_003f6df4 < -0x40)) &&
     (0x119 < field_003f6d9c || (0x119 < field_003f6df0 || 0x119 < field_003f6df4))) {
    return;
  }
  field_003f6d28 = param_1;
  sortTriangleVertices(m);
  if (field_003f6d9c < 1) {
    return;
  }
  if (field_0009d508 < field_003f6df0) {
    return;
  }
  field_003f6d54 = field_003f6df4 - field_003f6df0;
  field_003f6d50 = field_003f6d9c - field_003f6df0;
  field_003f6d60 = field_003f6d9c - field_003f6df4;
  iVar8 = field_003f6de8 - field_003f6de0;
  iVar4 = field_003f6de8 - field_003f6de4;
  iVar9 = field_003f6dd8 - field_003f6dd4;
  iVar5 = field_003f6df0 * field_003f6d78;
  iVar3 = field_003f6d84 - field_003f6d80;
  if (field_003f6d54 == 0) {
    if (field_003f6d60 == 0) {
      return;
    }
    if (field_003f6de0 == field_003f6de4) {
      return;
    }
    if (field_003f6de0 < field_003f6de4) {
      field_003f6d94 = field_003f6dd4 - field_003f6dd0;
      iVar3 = ((field_003f6de4 - field_003f6de0) + 0x8000) >> 0x10;
      field_003f6d4c = field_003f6d94 / iVar3;
      field_003f6d98 = field_003f6d80 - field_003f6d7c;
      field_003f6d48 = field_003f6d98 / iVar3;
      field_003f6dc8 = (field_003f6dd8 - field_003f6dd0) / field_003f6d50;
      field_003f6dc4 = (field_003f6d84 - field_003f6d7c) / field_003f6d50;
      field_003f6dcc = (field_003f6de8 - field_003f6de0) / field_003f6d50;
      field_003f6d44 = (field_003f6de8 - field_003f6de4) / field_003f6d50;
      field_003f6dac = field_003f6d7c;
      field_003f6db0 = field_003f6de0;
      field_003f6da8 = field_003f6dd0;
      field_003f6cf0 = field_0009d544 + field_003f6df0 * field_003f6d78;
      field_003f6d90 = field_003f6de4;
      field_003f6d58 = field_003f6d50;
      if (field_0009d508 < field_003f6d9c) {
        field_003f6d58 = (field_0009d508 + 1) - field_003f6df0;
      }
      field_003f6d68 = -field_003f6d4c;
      field_003f6d74 = -field_003f6d48;
      if (field_003f6df0 < 0) {
        iVar3 = -field_003f6df0;
        field_003f6d58 = field_003f6d58 + field_003f6df0;
        if (field_003f6d58 < 1) {
          field_003f6dac = field_003f6d7c;
          return;
        }
        field_003f6db0 = field_003f6de0 + field_003f6dcc * iVar3;
        field_003f6d90 = field_003f6de4 + field_003f6d44 * iVar3;
        field_003f6da8 = field_003f6dd0 + field_003f6dc8 * iVar3;
        field_003f6dac = field_003f6d7c + iVar3 * field_003f6dc4;
        field_003f6cf0 = field_0009d544;
      }
    }
    else {
      field_003f6d94 = field_003f6dd0 - field_003f6dd4;
      iVar6 = ((field_003f6de0 - field_003f6de4) + 0x8000) >> 0x10;
      field_003f6d4c = field_003f6d94 / iVar6;
      field_003f6d98 = field_003f6d7c - field_003f6d80;
      field_003f6d48 = field_003f6d98 / iVar6;
      field_003f6dc8 = iVar9 / field_003f6d60;
      field_003f6dc4 = iVar3 / field_003f6d60;
      field_003f6dcc = iVar4 / field_003f6d60;
      field_003f6d44 = iVar8 / field_003f6d60;
      field_003f6db0 = field_003f6de4;
      field_003f6d90 = field_003f6de0;
      field_003f6da8 = field_003f6dd4;
      field_003f6dac = field_003f6d80;
      field_003f6cf0 = field_0009d544 + iVar5;
      field_003f6d58 = field_003f6d60;
      if (field_0009d508 < field_003f6d9c) {
        field_003f6d58 = (field_0009d508 + 1) - field_003f6df4;
      }
      field_003f6d68 = -field_003f6d4c;
      field_003f6d74 = -field_003f6d48;
      if (field_003f6df0 < 0) {
        iVar3 = -field_003f6df0;
        field_003f6d58 = field_003f6d58 + field_003f6df0;
        if (field_003f6d58 < 1) {
          field_003f6dac = field_003f6d80;
          return;
        }
        field_003f6db0 = field_003f6de4 + field_003f6dcc * iVar3;
        field_003f6d90 = field_003f6de0 + field_003f6d44 * iVar3;
        field_003f6da8 = field_003f6dd4 + field_003f6dc8 * iVar3;
        field_003f6dac = field_003f6d80 + iVar3 * field_003f6dc4;
        field_003f6cf0 = field_0009d544;
      }
    }
  }
  else {
    iVar6 = field_003f6de4 - field_003f6de0;
    iVar7 = field_003f6d80 - field_003f6d7c;
    iVar10 = field_003f6dd4 - field_003f6dd0;
    if (field_003f6d60 == 0) {
      if (field_003f6de4 == field_003f6de8) {
        return;
      }
      iVar2 = (field_0009d508 + 1) - field_003f6df0;
      if (field_003f6de8 < field_003f6de4) {
        field_003f6d94 = field_003f6dd4 - field_003f6dd8;
        iVar3 = ((field_003f6de4 - field_003f6de8) + 0x8000) >> 0x10;
        field_003f6d4c = field_003f6d94 / iVar3;
        field_003f6d98 = field_003f6d80 - field_003f6d84;
        field_003f6d48 = field_003f6d98 / iVar3;
        field_003f6dc8 = (field_003f6dd8 - field_003f6dd0) / field_003f6d50;
        field_003f6dc4 = (field_003f6d84 - field_003f6d7c) / field_003f6d50;
        field_003f6dcc = iVar8 / field_003f6d50;
        field_003f6d44 = iVar6 / field_003f6d50;
        field_003f6db0 = field_003f6de0;
        field_003f6d90 = field_003f6de0;
        field_003f6da8 = field_003f6dd0;
        field_003f6dac = field_003f6d7c;
        field_003f6cf0 = field_0009d544 + iVar5;
        field_003f6d58 = field_003f6d50;
        if (field_0009d508 < field_003f6d9c) {
          field_003f6d58 = iVar2;
        }
        field_003f6d68 = -field_003f6d4c;
        field_003f6d74 = -field_003f6d48;
        if (field_003f6df0 < 0) {
          iVar3 = -field_003f6df0;
          field_003f6d58 = field_003f6d58 + field_003f6df0;
          if (field_003f6d58 < 1) {
            field_003f6dac = field_003f6d7c;
            return;
          }
          field_003f6db0 = field_003f6de0 + field_003f6dcc * iVar3;
          field_003f6d90 = field_003f6de0 + field_003f6d44 * iVar3;
          field_003f6da8 = field_003f6dd0 + field_003f6dc8 * iVar3;
          field_003f6dac = field_003f6d7c + iVar3 * field_003f6dc4;
          field_003f6cf0 = field_0009d544;
        }
      }
      else {
        iVar4 = (iVar4 + 0x8000) >> 0x10;
        field_003f6d4c = iVar9 / iVar4;
        field_003f6d48 = iVar3 / iVar4;
        field_003f6dc8 = iVar10 / field_003f6d54;
        field_003f6dc4 = iVar7 / field_003f6d54;
        field_003f6dcc = iVar6 / field_003f6d54;
        field_003f6d44 = iVar8 / field_003f6d54;
        field_003f6db0 = field_003f6de0;
        field_003f6d90 = field_003f6de0;
        field_003f6da8 = field_003f6dd0;
        field_003f6dac = field_003f6d7c;
        field_003f6cf0 = field_0009d544 + iVar5;
        field_003f6d58 = field_003f6d54;
        if (field_0009d508 < field_003f6df4) {
          field_003f6d58 = iVar2;
        }
        field_003f6d68 = -field_003f6d4c;
        field_003f6d74 = -field_003f6d48;
        field_003f6d94 = iVar9;
        field_003f6d98 = iVar3;
        if (field_003f6df0 < 0) {
          iVar8 = -field_003f6df0;
          field_003f6d58 = field_003f6d58 + field_003f6df0;
          if (field_003f6d58 < 1) {
            field_003f6dac = field_003f6d7c;
            return;
          }
          field_003f6db0 = field_003f6de0 + field_003f6dcc * iVar8;
          field_003f6d90 = field_003f6de0 + field_003f6d44 * iVar8;
          field_003f6da8 = field_003f6dd0 + field_003f6dc8 * iVar8;
          field_003f6dac = field_003f6d7c + iVar8 * field_003f6dc4;
          field_003f6cf0 = field_0009d544;
        }
      }
    }
    else {
      iVar6 = iVar6 / field_003f6d54;
      iVar8 = iVar8 / field_003f6d50;
      iVar4 = iVar4 / field_003f6d60;
      iVar5 = iVar6 * field_003f6d50 + field_003f6de0;
      if (iVar6 < iVar8) {
        field_003f6dc8 = iVar10 / field_003f6d54;
        field_003f6dc4 = iVar7 / field_003f6d54;
        iVar9 = iVar9 / field_003f6d60;
        iVar3 = iVar3 / field_003f6d60;
        iVar5 = ((field_003f6de8 - iVar5) + 0x8000) >> 0x10;
        if (iVar5 == 0) {
          iVar5 = 1;
        }
        field_003f6d94 = field_003f6dd8 - (field_003f6d50 * field_003f6dc8 + field_003f6dd0);
        field_003f6d4c = field_003f6d94 / iVar5;
        field_003f6d98 = field_003f6d84 - (field_003f6dc4 * field_003f6d50 + field_003f6d7c);
        field_003f6d48 = field_003f6d98 / iVar5;
        field_003f6da8 = field_003f6dd0;
        field_003f6dac = field_003f6d7c;
        field_003f6db0 = field_003f6de0;
        field_003f6d90 = field_003f6de0;
        field_003f6d68 = -field_003f6d4c;
        field_003f6d74 = -field_003f6d48;
        field_003f6d44 = iVar8;
        const int lowerVStep = iVar3;
        if (field_003f6df4 < 1) {
          field_003f6cf0 = field_0009d544;
        }
        else {
          field_003f6dcc = iVar6;
          if (field_003f6df0 < 0) {
            iVar3 = -field_003f6df0;
            field_003f6cf0 = field_0009d544;
            field_003f6d58 = field_003f6df4;
            field_003f6db0 = field_003f6de0 + iVar6 * iVar3;
            field_003f6d90 = field_003f6de0 + iVar8 * iVar3;
            field_003f6da8 = field_003f6dd0 + field_003f6dc8 * iVar3;
            field_003f6dac = field_003f6d7c + iVar3 * field_003f6dc4;
            if (field_0009d508 < field_003f6df4) {
              uVar1 = (unsigned)m.b(0x3f6e83);
              field_003f6d58 = field_0009d508;
              goto LAB_0004bc16;
            }
          }
          else {
            field_003f6cf0 = field_0009d544 + field_003f6df0 * field_003f6d78;
            field_003f6d58 = field_003f6d54;
            if (field_0009d508 < field_003f6df4) {
              field_003f6d58 = (field_0009d508 + 1) - field_003f6df0;
              uVar1 = (unsigned)m.b(0x3f6e83);
              goto LAB_0004bc16;
            }
          }
          f(m,m.d(0x9d510+static_cast<unsigned>(m.b(0x3f6e83))*4),{});
          iVar3 = lowerVStep;
        }
        field_003f6da8 = field_003f6dd4;
        field_003f6dac = field_003f6d80;
        field_003f6db0 = field_003f6de4;
        field_003f6d58 = field_003f6d60;
        if (field_0009d508 < field_003f6d9c) {
          field_003f6d58 = (field_0009d508 + 1) - field_003f6df4;
        }
        field_003f6d68 = -field_003f6d4c;
        field_003f6d74 = -field_003f6d48;
        field_003f6dc4 = iVar3;
        field_003f6dc8 = iVar9;
        field_003f6dcc = iVar4;
        if (field_003f6df4 < 1) {
          if (field_003f6d9c < 1) {
            field_003f6dac = field_003f6d80;
            return;
          }
          field_003f6d58 = field_003f6d9c;
          field_003f6cf0 = field_0009d544;
          if (field_0009d508 < field_003f6d9c) {
            field_003f6d58 = field_0009d508;
          }
          iVar8 = -field_003f6df4;
          field_003f6db0 = field_003f6de4 + iVar4 * iVar8;
          field_003f6d90 = field_003f6de0 + field_003f6d44 * -field_003f6df0;
          field_003f6da8 = field_003f6dd4 + iVar9 * iVar8;
          field_003f6dac = field_003f6d80 + iVar8 * iVar3;
        }
      }
      else {
        if (iVar6 == iVar8) {
          return;
        }
        field_003f6dc8 = (field_003f6dd8 - field_003f6dd0) / field_003f6d50;
        field_003f6dc4 = (field_003f6d84 - field_003f6d7c) / field_003f6d50;
        iVar3 = ((iVar5 - field_003f6de8) + 0x8000) >> 0x10;
        if (iVar3 == 0) {
          iVar3 = 1;
        }
        field_003f6d94 = (field_003f6d50 * (iVar10 / field_003f6d54) + field_003f6dd0) - field_003f6dd8;
        field_003f6d4c = field_003f6d94 / iVar3;
        field_003f6d98 = ((iVar7 / field_003f6d54) * field_003f6d50 + field_003f6d7c) - field_003f6d84;
        field_003f6d48 = field_003f6d98 / iVar3;
        field_003f6da8 = field_003f6dd0;
        field_003f6db0 = field_003f6de0;
        field_003f6d90 = field_003f6de0;
        field_003f6d68 = -field_003f6d4c;
        field_003f6dac = field_003f6d7c;
        field_003f6d74 = -field_003f6d48;
        field_003f6dcc = iVar8;
        if (field_003f6df4 < 1) {
          field_003f6cf0 = field_0009d544;
        }
        else {
          field_003f6d44 = iVar6;
          if (field_003f6df0 < 0) {
            iVar3 = -field_003f6df0;
            field_003f6cf0 = field_0009d544;
            field_003f6d58 = field_003f6df4;
            field_003f6db0 = field_003f6de0 + iVar8 * iVar3;
            field_003f6d90 = field_003f6de0 + iVar6 * iVar3;
            field_003f6da8 = field_003f6dd0 + field_003f6dc8 * iVar3;
            field_003f6dac = field_003f6d7c + iVar3 * field_003f6dc4;
            if (field_0009d508 < field_003f6df4) {
              field_003f6d58 = field_0009d508;
              f(m,m.d(0x9d510+static_cast<unsigned>(m.b(0x3f6e83))*4),{});
              return;
            }
          }
          else {
            field_003f6cf0 = field_0009d544 + field_003f6df0 * field_003f6d78;
            field_003f6d58 = field_003f6d54;
            if (field_0009d508 < field_003f6df4) {
              field_003f6d58 = (field_0009d508 + 1) - field_003f6df0;
              f(m,m.d(0x9d510+static_cast<unsigned>(m.b(0x3f6e83))*4),{});
              return;
            }
          }
          f(m,m.d(0x9d510+static_cast<unsigned>(m.b(0x3f6e83))*4),{});
        }
        field_003f6d90 = field_003f6de4;
        field_003f6d58 = field_003f6d60;
        if (field_0009d508 < field_003f6d9c) {
          field_003f6d58 = (field_0009d508 + 1) - field_003f6df4;
        }
        field_003f6d44 = iVar4;
        if (field_003f6df4 < 1) {
          if (field_003f6d9c == 0) {
            return;
          }
          field_003f6d58 = field_003f6d9c;
          field_003f6cf0 = field_0009d544;
          if (field_0009d508 < field_003f6d9c) {
            field_003f6d58 = field_0009d508;
          }
          iVar3 = -field_003f6df0;
          field_003f6db0 = field_003f6de0 + field_003f6dcc * iVar3;
          field_003f6d90 = field_003f6de4 + iVar4 * -field_003f6df4;
          field_003f6da8 = field_003f6dd0 + field_003f6dc8 * iVar3;
          field_003f6dac = field_003f6dc4 * iVar3 + field_003f6d7c;
        }
      }
    }
  }
  uVar1 = (unsigned)m.b(0x3f6e83);
LAB_0004bc16:
  f(m,m.d(0x9d510+static_cast<unsigned>(uVar1)*4),{});
  return;
}

void rasterizeSolidSpans(Mem m){
    auto color=m.b(0x3f6e79);m.d(0x5b3fc,color*0x01010101u);
    auto rows=m.d(0x3f6d58)&255u;m.d(0x3f6d58,rows);
    if(!rows)return;
    auto pointer=m.d(0x3f6cf0),buffer=m.d(0x9d544);
    if(pointer<buffer||pointer>=buffer+0x12ac0u)return;
    do {
        int right=m.s(0x3f6d92),left=m.s(0x3f6db2);
        if(right>=0&&left<si(m.d(0x9d504))){
            unsigned end=m.w(0x3f6d92),maximum=m.w(0x9d504);if(end>maximum)end=maximum;
            auto dest=m.d(0x3f6cf0);unsigned count;
            if(left<0)count=(end+1)&255u;
            else{dest+=static_cast<unsigned>(left);count=(end-static_cast<unsigned>(left)+1)&255u;}
            for(unsigned i=0;i<count;++i)m.b(dest+i,color);
        }
        m.d(0x3f6db0,m.d(0x3f6db0)+m.d(0x3f6dcc));m.d(0x3f6d90,m.d(0x3f6d90)+m.d(0x3f6d44));m.d(0x3f6cf0,m.d(0x3f6cf0)+320);
        m.d(0x3f6d58,m.d(0x3f6d58)-1);
    }while(si(m.d(0x3f6d58))>=0);
}
void rasterizeShadedSprite(Mem m){
    auto red=m.d(0x3f6da4)+m.d(0x5c4fa),green=m.d(0x3f6dbc)+m.d(0x5c4f4),blue=m.d(0x3f6d8c)+m.d(0x5c4ee);
    m.d(0x5c599,red);m.d(0x5c593,green);m.d(0x5c587,blue);
    auto dest=m.d(0x3f6cf0),source=m.d(0x3f6d28),rows=m.d(0x3f6d2c);
    // Original counts require a positive height; width low byte zero means 256.
    do {
        auto s=source,d=dest;auto width=m.d(0x3f6d70)&255u;if(!width)width=256;
        for(unsigned i=0;i<width;++i){
            auto index=m.b(s);s=(s&0xffffff00u)|((s+1)&255u);
            if(index){auto rgb15=m.d(blue+m.b(0x3f2bf0+index))|m.d(green+m.b(0x3f29f0+index))|m.d(red+m.b(0x3f2af0+index));m.b(d,m.b(0x3e89d8+rgb15));}
            ++d;
        }
        dest+=320;source=(source&0xffff00ffu)|((source+256)&0xff00u);
    }while(--rows);
}
void renderShadedSpritePacket(Mem m,unsigned p){
    m.b(0x3f6e7d,m.b(p+8)>>2);m.b(0x3f6e7c,m.b(p+9)>>2);m.b(0x3f6e7b,m.b(p+10)>>2);
    m.d(0x3f6e40,m.d(0x9d544));m.d(0x3f6da4,m.b(0x3f6e7d)<<7);m.d(0x3f6dbc,m.b(0x3f6e7c)<<7);m.d(0x9d544,0x28c888);m.d(0x3f6d8c,m.b(0x3f6e7b)<<7);
    m.d(0x3f6cf0,0x28c888u+static_cast<unsigned>(m.s(p+14))*320+static_cast<unsigned>(m.s(p+12)));
    auto v=m.b(p+17);auto texture=resolveTexturePageBuffer(m,m.w(p+26));m.d(0x3f6d28,texture+v*256+m.b(p+16));
    m.d(0x3f6d70,static_cast<unsigned>(m.s(p+20)-m.s(p+12)));m.d(0x3f6d2c,static_cast<unsigned>(m.s(p+30)-m.s(p+14)));
    rasterizeShadedSprite(m);m.d(0x9d544,m.d(0x3f6e40));
}
namespace {
void indexedTextureSpans(Mem m,bool transparent){
    auto rows=m.d(0x3f6d58)&255u;m.d(0x3f6d58,rows);if(!rows)return;
    auto buffer=m.d(0x9d544),pointer=m.d(0x3f6cf0);if(pointer<buffer||pointer>=buffer+0x12ac0u)return;
    auto du=m.d(0x3f6d4c),dv=m.d(0x3f6d48);
    const unsigned patches[2][3][7]={
        {{0x5a34a,0x5a35f,0x5a374,0x5a388,0x5a3a8,0x5a3c1,0x5a3da},{0x5a33f,0x5a354,0x5a369,0x5a37b,0x5a39d,0x5a3b6,0x5a3cf},{0x5a342,0x5a357,0x5a36c,0x5a37e,0x5a3a0,0x5a3b9,0x5a3d2}},
        {{0x5a743,0x5a75b,0x5a774,0x5a78d,0x5a7b2,0x5a7ce,0x5a7eb},{0x5a73a,0x5a752,0x5a76b,0x5a784,0x5a7a9,0x5a7c5,0x5a7e2},{0x5a73d,0x5a755,0x5a76e,0x5a787,0x5a7ac,0x5a7c8,0x5a7e5}}
    };
    for(auto a:patches[transparent][0])m.b(a,du>>16);for(auto a:patches[transparent][1])m.b(a,dv>>16);for(auto a:patches[transparent][2])m.d(a,du<<16);
    auto slope=m.d(0x3f6dcc),fraction=(si(slope)<0?0u-slope:slope)&65535u;
    m.d(0x3f6d34,static_cast<unsigned>((static_cast<std::int64_t>(si(du))*fraction)>>16));
    m.d(0x3f6d30,static_cast<unsigned>((static_cast<std::int64_t>(si(dv))*fraction)>>16));
    do{
        int left=m.s(0x3f6db2),right=m.s(0x3f6d92);
        if(right>=0&&left<si(m.d(0x9d504))){
            auto u=m.d(0x3f6da8)-m.d(0x3f6d68),v=m.d(0x3f6dac)-m.d(0x3f6d74);m.d(0x3f6db8,u);m.d(0x3f6dc0,v);
            auto dest=m.d(0x3f6cf0);unsigned end=m.w(0x3f6d92);if(end>m.w(0x9d504))end=m.w(0x9d504);
            unsigned count;auto scratch=transparent?0x5a4ccu:0x5a0ecu;
            if(left<0){auto clip=0u-static_cast<unsigned>(left);m.d(scratch,clip);u+=du*clip;v+=dv*clip;m.d(scratch+4,u);m.d(scratch+8,v);count=end&65535u;}
            else{dest+=static_cast<unsigned>(left);count=(end-static_cast<unsigned>(left))&65535u;}
            unsigned remainder=0;
            if(transparent){m.b(0x5a4dc,0);if(count&0xff00u){remainder=(count+1)&255u;m.b(0x5a4dc,remainder);count=255;}}
            else count&=255u;
            if(count){
                auto source=(m.d(0x3f6d28)&0xffff0000u)|((v>>8)&0xff00u)|((u>>16)&255u);
                unsigned uf=u<<16,vf=v<<16;
                do {
                auto old=vf;vf+=0x80000000u;if(vf<old)source=(source&0xffff00ffu)|((source+256)&0xff00u);
                old=uf;uf+=0x80000000u;if(uf<old)++source;
                for(unsigned i=0;i<count;++i){
                    auto index=m.b(source);unsigned color=0;if(!transparent||index)color=m.b((m.d(0x3f6d3c)&0xffffff00u)|index);
                    old=vf;vf+=dv<<16;unsigned nextV=((source>>8)+(dv>>16)+(vf<old))&255u;
                    old=uf;uf+=du<<16;unsigned nextU=(source+(du>>16)+(uf<old))&255u;
                    source=(source&0xffff0000u)|(nextV<<8)|nextU;if(!transparent||index)m.b(dest+i,color);
                }
                if(!remainder)break;
                // Original long transparent rows restart at the last block,
                // overlapping the three tail pixels and repeating UV rounding.
                dest+=count&~3u;count=remainder;remainder=0;m.b(0x5a4dc,0);
                }while(true);
            }
        }
        m.d(0x3f6d90,m.d(0x3f6d90)+m.d(0x3f6d44));m.d(0x3f6da8,m.d(0x3f6da8)+m.d(0x3f6dc8));m.d(0x3f6dac,m.d(0x3f6dac)+m.d(0x3f6dc4));m.d(0x3f6cf0,m.d(0x3f6cf0)+320);
        auto previous=m.d(0x3f6db0),next=previous+slope,cu=m.d(0x3f6d68),cv=m.d(0x3f6d74);m.d(0x3f6db0,next);
        if(si(slope)>=0){if((previous&65535u)>(next&65535u)){cu-=du;cv-=dv;}cu+=m.d(0x3f6d34);cv+=m.d(0x3f6d30);}
        else{if((previous&65535u)<(next&65535u)){cu+=du;cv+=dv;}cu-=m.d(0x3f6d34);cv-=m.d(0x3f6d30);}
        m.d(0x3f6d68,cu);m.d(0x3f6d74,cv);m.d(0x3f6d58,m.d(0x3f6d58)-1);
    }while(m.d(0x3f6d58));
}
}
void rasterizeIndexedTextureSpans(Mem m){indexedTextureSpans(m,false);}
void rasterizeTransparentTextureSpans(Mem m){indexedTextureSpans(m,true);}
RenderService withPolygonRasterizer(const RenderService& external){return [external](Mem& m,unsigned routine,std::span<const unsigned> a)->unsigned{
    auto spans=[&](Mem& mem,unsigned address,std::span<const unsigned> args)->unsigned{if(address==0x5b428){rasterizeSolidSpans(mem);return 0;}if(address==0x5a186){rasterizeIndexedTextureSpans(mem);return 0;}if(address==0x5a567){rasterizeTransparentTextureSpans(mem);return 0;}if(address==0x5a991){rasterizeShadedTextureSpans(mem);return 0;}if(address==0x5ad18){rasterizeTransparentShadedSpans(mem);return 0;}if(address==0x5b0f7){rasterizeBlendedShadedSpans(mem);return 0;}return external(mem,address,args);};
    switch(routine){case 0x4acb8:setupTriangleSpans(m,a.empty()?0:a[0],spans);break;case 0x4a0e0:setupTexturedRectangle(m,a[0],spans);break;case 0x5b428:rasterizeSolidSpans(m);break;case 0x5c519:rasterizeShadedSprite(m);break;case 0x4abc0:renderShadedSpritePacket(m,a[0]);break;case 0x5a186:rasterizeIndexedTextureSpans(m);break;case 0x5a567:rasterizeTransparentTextureSpans(m);break;case 0x5a991:rasterizeShadedTextureSpans(m);break;case 0x5ad18:rasterizeTransparentShadedSpans(m);break;case 0x5b0f7:rasterizeBlendedShadedSpans(m);break;default:return external(m,routine,a);}return 0;
};}
}
