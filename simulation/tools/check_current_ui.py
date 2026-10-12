"""当前GUI与迁移路径检查；输出仅进入指定任务目录。"""
from pathlib import Path
import argparse,hashlib,json,sys,tkinter as tk
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from app import App,desktop_dpi,window_handle
from parameters import defaults,load,save
from PIL import ImageGrab

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    desktop_dpi();root=tk.Tk();app=App(root);root.update()
    try:
        p=app.collect();assert p==defaults()
        app.parameter_name.set('中速验证_300');app.select_params();root.update()
        assert app.collect()['height']==.297 and app.collect()['max_duty']==300
        app.fit('maximum');assert app.fit_candidate['height']==.297
        app.apply_fit();assert app.collect()['height']==.297
        for path in (ROOT/'configs').glob('*.json'):load(path)
        assert len(list((ROOT/'configs').glob('*.json')))==7
        assert all(path.stem not in app.preset_paths for path in (ROOT/'configs/archive').glob('*.json'))
        target=output/'roundtrip.json';save(target,app.collect());assert load(target)==app.collect()
        for size in ('1100x720','1480x930'):
            root.geometry(size);root.update();ImageGrab.grab(window=window_handle(root)).save(output/(size+'.png'))
        app.view_tabs.select(1);app.refresh();root.update()
        ImageGrab.grab(window=window_handle(root)).save(output/'3d.png')
        # A stale external path must not replace the built-in preset.
        assert app.preset_paths['我的参数'].resolve()==(ROOT/'configs/我的参数.json').resolve()
        app.start()
        # Include the center-settle interval and the controller's speed ramp.
        for _ in range(300):app.sim.tick()
        app.pause();assert app.sim.distance>.01, (app.sim.now, app.sim.data.tolist(), app.sim.distance)
        app.refresh();root.update()
        assert app.sim.image.shape==(120,188) and app.sim.image.max()>app.sim.image.min()
        assert app.camera_label.find_withtag('viewport-image')
        ImageGrab.grab(window=window_handle(root)).save(output/'camera-after-ticks.png')
        (output/'result.json').write_text(json.dumps({'passed':True,'height':app.collect()['height'],'frames':app.sim.frame_id,'distance_m':app.sim.distance},indent=2),encoding='utf-8')
        print('Current GUI: presets, fixed-height fit, roundtrip, small/large window and rendering passed.')
    finally:app.close()
if __name__=='__main__':main()
