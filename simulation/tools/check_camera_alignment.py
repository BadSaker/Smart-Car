"""对同批逐飞截图计算道路轮廓对照；只估计车姿，不把重合度当作相机标定证明。"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from parameters import load
from geometry import Track
from camera_optics import ground_intersections, straight_width
from comparison import read_measured_image
from render import font


def estimate_pose(track, parameters, gray, valid):
    """在现有赛道上搜索每帧车姿，固定相机参数；输出是图像配准估计。"""
    cols, rows = np.meshgrid(np.arange(1,188,4), np.arange(30,116,4))
    q, ray_valid = ground_intersections(cols, rows, parameters)
    forward = q[...,0] + parameters['camera_offset']
    left = q[...,1] + parameters.get('camera_lateral',0)
    target = gray[rows,cols] > 235
    mask = valid[rows,cols]

    def loss(pose):
        x,y,h = pose
        c,s = math.cos(h),math.sin(h)
        ix = np.rint((x+forward*c-left*s-track.minimum[0])/track.cell).astype(int)
        iy = np.rint((y+forward*s+left*c-track.minimum[1])/track.cell).astype(int)
        ok = ray_valid & (ix>=0) & (iy>=0) & (ix<track.raster.shape[1]) & (iy<track.raster.shape[0])
        rendered = np.zeros(ok.shape,bool)
        rendered[ok] = track.raster[iy[ok],ix[ok]] > 200
        union = np.count_nonzero((rendered|target)&mask)
        return 1-np.count_nonzero(rendered&target&mask)/max(1,union)

    candidates = []
    for x,y,h,_ in track.samples[::12]:
        for angle in (-.3,0.,.3):
            for offset in (-.08,0.,.08):
                pose = (x-math.sin(h)*offset,y+math.cos(h)*offset,h+angle)
                candidates.append((loss(pose),pose))
    candidates.sort(key=lambda item:item[0])
    best = candidates[0]
    for _,pose in candidates[:8]:
        at = np.array(pose)
        for step in (.04,.02,.01,.005,.002):
            for _ in range(12):
                changed = False
                current = loss(at)
                for axis in range(3):
                    for sign in (-1,1):
                        trial = at.copy();trial[axis] += sign*step
                        score = loss(trial)
                        if score+1e-10 < current:
                            at,current,changed = trial,score,True
                if not changed:break
        value = loss(at)
        if value < best[0]:best = (value,at)
    return list(best[1]),1-best[0]


def compare(images, output, before, after):
    output.mkdir(parents=True,exist_ok=True)
    profiles = {'before':before,'after':after}
    scene = {**before,'edge_width':0.,'gray_road':250,'gray_background':130,'noise':0.}
    search_track = Track(scene)
    tracks = {key:Track(p) for key,p in profiles.items()}
    rows = []
    panels = []
    for path in sorted(images.glob('*.png')):
        gray,valid,info = read_measured_image(path)
        mask = valid.copy();mask[:30] = False;mask[116:] = False
        # 同一照片的车姿未知，两组使用同一搜索范围、采样网格和计算预算。
        item = {'file':path.name,'source':info,'profiles':{}}
        rgb = np.repeat(gray[:,:,None],3,axis=2);rgb[~valid] = [150,65,150]
        pictures = [Image.fromarray(rgb)]
        for name,p in profiles.items():
            pose,coarse_iou = estimate_pose(search_track,p,gray,valid)
            image = tracks[name].camera(*pose,{**p,'noise':0.})
            target = gray>235
            binary = image>(p['gray_background']+p['gray_road'])/2
            union = np.count_nonzero((target|binary)&mask)
            iou = np.count_nonzero(target&binary&mask)/max(1,union)
            item['profiles'][name] = {'estimated_pose':pose,'coarse_iou':coarse_iou,
                'full_resolution_iou':iou,'valid_pixels':int(mask.sum())}
            pictures.append(Image.fromarray(image).convert('RGB'))
        panels.append((path.name,pictures));rows.append(item)
        print(path.name,round(item['profiles']['before']['full_resolution_iou'],4),
              round(item['profiles']['after']['full_resolution_iou'],4),flush=True)
    if not rows:raise ValueError('没有可比较的PNG截图。')
    # 分页显示真实图、旧模型、新模型；紫色为被遮挡像素，不补造内容。
    for page in range((len(panels)+3)//4):
        subset = panels[page*4:(page+1)*4]
        sheet = Image.new('RGB',(1176,66+len(subset)*276),'#f4f6f8')
        draw = ImageDraw.Draw(sheet)
        for column,title in enumerate(('截图参考（紫色遮挡）','修正前：53° / 120°','修正后：条件拟合')):
            draw.text((12+column*390,12),title,font=font(18),fill='#152b42')
        for index,(name,pictures) in enumerate(subset):
            y=52+index*276
            draw.text((12,y),name,font=font(11),fill='#345')
            for column,im in enumerate(pictures):
                sheet.paste(im.resize((376,240),Image.Resampling.NEAREST),(12+column*390,y+23))
        sheet.save(output/f'camera-comparison-{page+1}.png')
    widths=[]
    for row,target in zip((30,45,70,86,108,116),after['reference_widths']):
        widths.append({'row':row,'reference':target,'before':straight_width(before,row),
                       'after':straight_width(after,row)})
    report={'method':'每张截图在同一赛道中独立估计车姿，固定相机模型；30至115行，排除彩线/文字；不是已知车姿的独立标定验证。',
        'parameters':profiles,'rows':widths,'frames':rows,
        'mean_full_resolution_iou':{name:float(np.mean([r['profiles'][name]['full_resolution_iou'] for r in rows])) for name in profiles}}
    (output/'alignment-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report['mean_full_resolution_iou']))
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--images',type=Path,default=ROOT.parent/'tmp/figs')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--before',type=Path,default=ROOT/'configs/历史_297mm_53度.json')
    parser.add_argument('--after',type=Path,default=ROOT/'configs/我的参数.json')
    args=parser.parse_args()
    compare(args.images,args.output,load(args.before),load(args.after))
