"""Read-only measured-frame comparison. Never fits or applies camera parameters."""
import csv,json,math
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from camera_optics import project_ground
from geometry import predicted_width
from render import font


def read_measured_image(path):
    """读取原始帧或有完整左右标线的逐飞截图，保留遮挡掩码而不补造像素。"""
    with Image.open(path) as im:
        rgb=np.asarray(im.convert('RGB')).copy()
    if rgb.shape[:2]==(120,188):
        neutral=np.ptp(rgb.astype(int),axis=2)<=12
        gray=np.asarray(Image.fromarray(rgb).convert('L'))
        return gray,neutral,{'kind':'raw','note':'188×120输入；彩色覆盖像素已排除。'}
    r,g,b=[rgb[:,:,i].astype(int) for i in range(3)]
    green=(g>220)&(r<40)&(b<40)
    yellow=(r>220)&(g>220)&(b<40)
    scores=green.sum(axis=0);scores[rgb.shape[1]//2:]=0
    left=int(np.argmax(scores))
    if scores[left]<60:raise ValueError('无法识别完整逐飞图像边界；请使用原始188×120帧或完整上位机截图。')
    ys=np.flatnonzero(green[:,left]);top,bottom=int(ys.min()),int(ys.max())+1
    scores_right=yellow[top:bottom].sum(axis=0);scores_right[:left+188]=0
    right=int(np.argmax(scores_right))
    if scores_right[right]<60:raise ValueError('截图缺少完整右侧标线，不能可靠恢复像素坐标。')
    # 同一像素会占多列，用连续标线的中点抵消缩放块偏差。
    def stripe_center(mask,x):
        counts=mask[top:bottom].sum(axis=0);threshold=counts[x]*.95
        lo=hi=x
        while lo>0 and counts[lo-1]>=threshold:lo-=1
        while hi+1<len(counts) and counts[hi+1]>=threshold:hi+=1
        return (lo+hi)/2
    left_center=stripe_center(green,left);right_center=stripe_center(yellow,right)
    sx=(right_center-left_center)/187;sy=(bottom-top)/120
    if sx<1 or abs(sx/sy-1)>.04:raise ValueError('截图比例或边界不完整；不对未知缩放进行强制拉伸。')
    xx=left_center+np.arange(188)*sx;yy=top+(np.arange(120)+.5)*sy-.5
    samples=[]
    for dy in (-.2,0.,.2):
        for dx in (-.2,0.,.2):
            ix=np.clip(np.rint(xx+dx*sx).astype(int),0,rgb.shape[1]-1)
            iy=np.clip(np.rint(yy+dy*sy).astype(int),0,rgb.shape[0]-1)
            samples.append(rgb[iy[:,None],ix[None,:]])
    sampled=np.median(np.stack(samples),axis=0).astype(np.uint8)
    valid=np.ptp(sampled.astype(int),axis=2)<=12
    colored=~valid.copy()
    # 文字覆盖通常是一行密集彩色像素；屏蔽整条文本框及邻行。
    for row in range(120):
        covered=np.flatnonzero(colored[row,2:-2])+2
        if len(covered)>18:
            valid[max(0,row-2):min(120,row+3),max(0,covered.min()-2):min(188,covered.max()+3)]=False
    gray=np.asarray(Image.fromarray(sampled).convert('L')).copy()
    return gray,valid,{'kind':'screenshot','source_size':[rgb.shape[1],rgb.shape[0]],
        'roi':[left_center-.5*sx,top,right_center+.5*sx,bottom],
        'note':'截图近似采样，彩线/文字处无原始像素；不能作为无损灰度回放或已标定车姿。'}


class Comparison:
    def __init__(self):
        from calibration import ROWS,WIDTHS
        self.real=None;self.source=None;self.points=[];self.widths=dict(zip(ROWS,WIDTHS));self.version=0
        self.valid_mask=None;self.source_kind=None;self.source_info={}
    def load_image(self,path):
        gray,mask,info=read_measured_image(path)
        self.real=gray;self.valid_mask=mask;self.source_info=info;self.source_kind=info['kind']
        self.source=str(path);self.version+=1
    def add_point(self,x_mm,y_mm,u_px,v_px):
        values=[float(x_mm),float(y_mm),float(u_px),float(v_px)]
        if not all(math.isfinite(v) for v in values):raise ValueError('标定点必须为有限数值。')
        if not (0<=values[2]<=187 and 0<=values[3]<=119):raise ValueError('实测像素坐标超出188×120图像。')
        self.points.append(dict(zip(('x_mm','y_mm','u_px','v_px'),values)));self.version+=1
    def load_points(self,path):
        with Path(path).open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
        previous=self.points.copy()
        try:
            self.points=[]
            for row in rows:self.add_point(*(row[k] for k in ('x_mm','y_mm','u_px','v_px')))
        except Exception:self.points=previous;raise
        self.version+=1
    def result(self,p):
        results=[]
        if self.points:
            positions=np.array([[v['x_mm']/1000,v['y_mm']/1000] for v in self.points])
            pixels,visible=project_ground(positions,p)
            for n,(point,pixel,valid) in enumerate(zip(self.points,pixels,visible),1):
                predicted=pixel.tolist() if valid else None
                residual=(pixel-np.array([point['u_px'],point['v_px']])).tolist() if valid else None
                results.append({'id':n,**point,'predicted_px':predicted,'error_px':residual,
                  'in_front':bool(valid),'in_image':bool(valid and 0<=pixel[0]<188 and 0<=pixel[1]<120)})
        errors=[v['error_px'] for v in results if v['error_px'] is not None]
        rmse=float(np.sqrt(np.mean(np.sum(np.square(errors),axis=1)))) if errors else None
        widths=[]
        for row,reference in self.widths.items():
            predicted=predicted_width(p,row)
            widths.append({'row':row,'reference_px':reference,'predicted_px':predicted,
                           'error_px':predicted-reference if predicted is not None else None})
        return {'source_image':self.source,'source_info':self.source_info,'points':results,'rmse_px':rmse,'straight_widths':widths,
                'coordinate_system':'rear axle origin; x forward/y left, ground in mm',
                'width_note':'ideal centered straight road, reference widths entered manually; not fitted'}
    def image(self,sim,size):
        w,h=size;im=Image.new('RGB',size,'#101d2d');d=ImageDraw.Draw(im);result=self.result(sim.p)
        stacked=False;gap=12;cell_w=(w-3*gap)//2
        reserved=80+22*len(self.widths)
        available=(h-reserved-60)//2 if stacked else h-reserved
        scale=max(.2,min(cell_w/188,max(60,available)/120));iw=round(188*scale);ih=round(120*scale)
        panels=[('截图参考（遮挡处紫色）' if self.source_kind=='screenshot' else '实测',self.real),('仿真',sim.image)]
        for index,(title,data) in enumerate(panels):
            x=gap if stacked else gap+index*(cell_w+gap);y=40+index*(ih+36) if stacked else 40
            d.text((x,y-26),title,font=font(14),fill='#dce6f2')
            if data is not None:
                pixels=np.repeat(data[:,:,None],3,axis=2)
                if index==0 and self.valid_mask is not None:pixels[~self.valid_mask]=[125,65,125]
                image=Image.fromarray(pixels).resize((iw,ih),Image.Resampling.NEAREST);im.paste(image,(x,y))
            else:
                d.rectangle((x,y,x+iw,y+ih),fill='#1d2b3d');d.text((x+8,y+12),'导入原始灰度帧',font=font(12),fill='#a8bbcf')
            for point in result['points']:
                position=[point['u_px'],point['v_px']] if index==0 else point['predicted_px']
                if position is None or not (0<=position[0]<188 and 0<=position[1]<120):continue
                px=x+position[0]*scale;py=y+position[1]*scale
                d.line((px-4,py,px+4,py),fill='#ffae56',width=2);d.line((px,py-4,px,py+4),fill='#ffae56',width=2)
                d.text((px+5,py-12),str(point['id']),font=font(11),fill='#ffae56')
            for row in self.widths:
                py=y+row*scale;d.line((x,py,x+iw,py),fill='#62cbbd',width=1)
        y=92+2*ih if stacked else 60+ih
        for v in result['straight_widths']:
            actual='无地面交点' if v['predicted_px'] is None else f"{v['predicted_px']:.2f}px，差 {v['error_px']:+.2f}px"
            d.text((gap,y),f"第{v['row']}行  参考 {v['reference_px']:g}px  直道预测 {actual}",font=font(11),fill='#dce6f2');y+=22
        text='尚未添加标定点' if result['rmse_px'] is None else f"标定点 {len(result['points'])} 个，像素误差 RMSE {result['rmse_px']:.2f}"
        d.text((gap,y),text,font=font(11),fill='#a8bbcf')
        return im
    def export(self,folder,p,sim):
        folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
        (folder/'comparison.json').write_text(json.dumps(self.result(p),ensure_ascii=False,indent=2),encoding='utf-8')
        (folder/'parameters.json').write_text(json.dumps(p,ensure_ascii=False,indent=2),encoding='utf-8')
        if self.real is not None:Image.fromarray(self.real).save(folder/'measured.png')
        if self.valid_mask is not None:Image.fromarray(self.valid_mask.astype(np.uint8)*255).save(folder/'measured-valid-mask.png')
        Image.fromarray(sim.image).save(folder/'simulated.png');self.image(sim,(960,620)).save(folder/'comparison.png')
        with (folder/'points.csv').open('w',encoding='utf-8-sig',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=['x_mm','y_mm','u_px','v_px']);writer.writeheader();writer.writerows(self.points)
        return folder
