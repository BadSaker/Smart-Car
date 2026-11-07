"""以冻结仿真灰度序列复现普通弯道误判、真实分叉入环，使用实际C接口。"""
import ctypes as C
import json
import os
import re
from pathlib import Path
import subprocess
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[2]
SCRATCH=ROOT/'tmp/control-tuning/element-tests'
COMPILER=os.environ.get('AUTOCAR_HOST_CC',str(ROOT/'simulation/toolchain/mingw64/bin/gcc.exe'))


class ElementSequenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        wrapper=SCRATCH/'element_bridge.c'
        wrapper.write_text('''#include "track_vision.h"
static track_vision_result_t output;
__declspec(dllexport) void reset(void) { track_vision_reset(); }
__declspec(dllexport) void frame(const unsigned char *p,unsigned ms,unsigned mm,int *v) {
 track_vision_process(p,ms,mm,&output);
 v[0]=output.element;v[1]=output.side;v[2]=output.has_path;
 v[5]=output.curve_outer_side!=TRACK_SIDE_NONE;
 v[6]=output.near_error_px;v[7]=output.far_error_px;v[8]=output.curve_outer_side;
}
''',encoding='utf-8')
        dll=SCRATCH/'element-tests.dll'
        cmd=[COMPILER,'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror','-static-libgcc',
             '-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),
             str(wrapper),str(ROOT/'car/src/track_vision.c'),'-o',str(dll)]
        result=subprocess.run(cmd,capture_output=True,text=True,errors='replace')
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        cls.lib=C.CDLL(str(dll))
        cls.lib.reset.argtypes=[];cls.lib.reset.restype=None
        cls.lib.frame.argtypes=[C.POINTER(C.c_ubyte),C.c_uint,C.c_uint,C.POINTER(C.c_int)]
        cls.lib.frame.restype=None

    def sequence(self,name,max_frames=None):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/element-sequences.zip') as archive:
            meta=json.loads(archive.read('metadata.json'));info=meta['segments'][name]
            data=bytearray(archive.read(info['file']))
        self.lib.reset();result=[];values=(C.c_int*18)()
        for i in range(min(info['frames'],max_frames or info['frames'])):
            frame=(C.c_ubyte*(188*120)).from_buffer(data,i*188*120)
            distance=info['distances_mm'][i] if 'distances_mm' in info else info['distance_start_mm']+i*info['step_mm']
            self.lib.frame(frame,i*meta['frame_ms'],distance,values)
            result.append(tuple(values))
        return result

    @staticmethod
    def cross_order_image(bend=0,top=48,bottom=70,provisional=False):
        image=bytearray([20])*22560
        for row in range(120):
            width=max(6,44+(row-30)*123//86);center=93+bend*(116-row)//86
            for col in range(max(0,center-width//2),min(188,center+width//2+1)):
                image[row*188+col]=220
        for row in range(top,bottom+1):image[row*188:(row+1)*188]=bytes([220])*188
        if provisional:
            for row in range(30,top):image[row*188:row*188+94]=bytes([220])*94
        return image

    @staticmethod
    def incomplete_cross_image(bottom=85,mirror=False,hide_exit=False):
        image=ElementSequenceTests.cross_order_image(20,48,bottom)
        for row in range(bottom+1,120):image[row*188:row*188+94]=bytes([220])*94
        if hide_exit:image[:48*188]=bytes([20])*(48*188)
        if mirror:image=bytearray().join(image[row*188:(row+1)*188][::-1] for row in range(120))
        return image

    def test_incomplete_cross_uses_recent_direction_and_current_exit(self):
        for mirror in (False,True):
            for bottom in (70,85,98):
                with self.subTest(mirror=mirror,bottom=bottom):
                    self.lib.reset();plain=self.cross_order_image(20,0,-1)
                    if mirror:plain=bytearray().join(plain[row*188:(row+1)*188][::-1] for row in range(120))
                    for n in range(3):self.feed(plain,n*20,n*16)
                    first=self.feed(self.incomplete_cross_image(bottom,mirror),60,48)
                    self.assertEqual(first[0],2)

    def test_incomplete_cross_without_recent_direction_keeps_confirmation(self):
        image=self.incomplete_cross_image()
        self.lib.reset();self.assertNotEqual(self.feed(image,0,0)[0],2)
        self.assertEqual(self.feed(image,20,16)[0],2)
        self.lib.reset();plain=self.cross_order_image(20,0,-1)
        for n in range(3):self.feed(plain,n*20,n*16)
        self.assertNotEqual(self.feed(image,400,320)[0],2)

    def test_incomplete_cross_without_visible_exit_does_not_use_preview(self):
        self.lib.reset();plain=self.cross_order_image(20,0,-1)
        for n in range(3):self.feed(plain,n*20,n*16)
        self.assertNotEqual(self.feed(self.incomplete_cross_image(hide_exit=True),60,48)[0],2)

    def test_bounded_cross_enters_on_first_frame(self):
        for bend in (0,20,-20):
            for top,bottom in ((48,70),(48,85),(60,98)):
                with self.subTest(bend=bend,top=top,bottom=bottom):
                    self.lib.reset();image=self.cross_order_image(bend,top,bottom)
                    self.assertEqual(self.feed(image,0,0)[0],2)

    def test_weak_cross_still_requires_two_frames(self):
        self.lib.reset();image=self.cross_order_image(20,provisional=True)
        self.assertNotEqual(self.feed(image,0,0)[0],2)
        self.assertEqual(self.feed(image,20,16)[0],2)

    def test_far_cross_stays_cross_until_near_opening_arrives(self):
        for bend in (0,20):
            self.lib.reset();image=self.cross_order_image(bend)
            frames=[self.feed(image,n*20,0) for n in range(20)]
            self.assertTrue(all(v[0]==2 for v in frames[1:]),[v[0] for v in frames])
            near=self.cross_order_image(bend,60,112)
            for n in range(4):self.assertEqual(self.feed(near,400+n*20,200+n*16)[0],2)
            clear=self.cross_order_image(bend,0,-1)
            for n in range(4):last=self.feed(clear,500+n*20,400+n*16)
            self.assertLess(last[0],2,'near opening then closure must release the cross')

    def test_visible_provisional_cross_keeps_path_after_curve_recheck_time(self):
        self.lib.reset();image=self.cross_order_image(20,provisional=True)
        frames=[self.feed(image,n*20,n*16) for n in range(25)]
        self.assertTrue(all(v[0]==2 for v in frames[1:]),[v[0] for v in frames])
        self.assertTrue(all(v[6:8]==frames[2][6:8] for v in frames[3:]),
                        'unchanged visible cross must not switch to curve reconstruction after 200ms')

    def test_ordinary_curves_do_not_latch_roundabout(self):
        for scene in ('ordinary_entry','ordinary_u','ordinary_s'):
            with self.subTest(scene=scene):
                result=self.sequence(scene)
                self.assertFalse(any(v[0]>=3 for v in result),'普通弯道被标为环岛')
                self.assertFalse(any(v[0]==2 for v in result),'普通弯道被标为十字')

    def test_visible_roundabout_fork_advances_to_inside(self):
        result=self.sequence('ring_right');phases={v[0] for v in result}
        self.assertIn(4,phases,'真实入口没有进入入环阶段')
        self.assertIn(5,phases,'入环后没有进入环内阶段')

    def test_reverse_s_bend_is_not_a_roundabout_entry(self):
        result=self.sequence('reverse_s_false')
        self.assertFalse(any(v[0]>=3 for v in result),'反向S弯上的远处分支不是本车环岛入口')

    def test_visible_outer_edge_supports_curved_corridor(self):
        result=self.sequence('curve_offscreen')[-1]
        self.assertEqual(result[2],1,'曲线白区延伸至画面外时仍有可靠外边')
        self.assertEqual(result[5],1,'运行中的普通弯道应使用连续外边支持')


    @staticmethod
    def fork_image(bottom,black_far=False):
        image=bytearray([20])*(188*120)
        for row in range(30,117):
            width=44+(row-30)*123//86
            left=93-width//2;right=93+width//2
            image[row*188+left:row*188+right+1]=bytes([220])*(right-left+1)
        for row in range(47,bottom+1):image[row*188:row*188+94]=bytes([220])*94
        # 缺口近侧加入圆弧形岛边；直线T口不提供驶入环岛的方向证据。
        for row in range(bottom+1,117):
            span=116-bottom;position=row-bottom
            bulge=16*position*(span-position)//(span*span)
            width=44+(row-30)*123//86;left=93-width//2
            image[row*188+left:row*188+left+bulge]=bytes([20])*bulge
        if black_far:image[:45*188]=bytes([20])*(45*188)
        return image

    def feed(self,image,ms,mm):
        values=(C.c_int*18)();pixels=(C.c_ubyte*len(image)).from_buffer(image)
        self.lib.frame(pixels,ms,mm,values)
        return tuple(values)




    def test_black_forward_branch_cannot_commit_entry(self):
        self.lib.reset();low=self.fork_image(79);hidden=self.fork_image(96,True)
        for ms in (0,20,40):result=self.feed(low,ms,ms//4)
        self.assertEqual(result[0],3)
        phases=[self.feed(hidden,ms,ms//4)[0] for ms in (60,80,100)]
        self.assertNotIn(4,phases,'旧分叉证据不能为全黑远场提交入环')


    def test_single_mode_reset_clears_candidate_votes(self):
        self.lib.reset();image=self.fork_image(79)
        self.feed(image,0,0);self.lib.reset()
        self.assertNotEqual(self.feed(image,20,5)[0],3)
        self.assertEqual(self.feed(image,40,10)[0],3)

    def test_enclosed_island_can_enter_without_a_two_ended_scan_gap(self):
        for mirror in (False,True):
            image=self.fork_image(79)
            for row in range(25,81):image[row*188:row*188+94]=bytes([220])*94
            for row in range(42,69):
                half=int(20*(max(0,1-((row-55)/14)**2))**.5)
                image[row*188+55-half:row*188+56+half]=bytes([20])*(2*half+1)
            if mirror:image=bytearray().join(image[y*188:(y+1)*188][::-1] for y in range(120))
            self.lib.reset();phases=[]
            for n in range(8):phases.append(self.feed(image,n*20,n*5)[0])
            self.assertIn(3,phases);self.assertIn(4,phases)

    def test_rectangular_dark_block_does_not_supply_enclosed_island_evidence(self):
        image=self.fork_image(79)
        for row in range(25,117):
            width=44+(row-30)*123//86;left=93-width//2
            image[row*188+max(0,left):row*188+94]=bytes([220])*(94-max(0,left))
        for row in range(25,81):image[row*188:row*188+94]=bytes([220])*94
        for row in range(42,69):image[row*188+35:row*188+76]=bytes([20])*41
        self.lib.reset()
        self.assertFalse(any(self.feed(image,n*20,n*5)[0]>=4 for n in range(8)))

    def test_recorded_cross_exit_association_and_island_entry(self):
        # Frozen recording pixels have interpolated overlays; zero-distance replay
        # checks perception only. Exact adjacent duplicates are not new samples.
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/video1438-cross-ring.zip') as z:
            segments=json.loads(z.read('metadata.json'))['segments']
            for name,info in segments.items():
                self.lib.reset();raw=z.read(info['file']);results={}
                for n,m in enumerate(info['frames']):
                    results[m['frame']]=self.feed(bytearray(raw[n*22560:(n+1)*22560]),m['ms'],m['mm'])
                if name=='20-24.5':
                    self.assertIn(4,{v[0] for v in results.values()})
                for frame in (1138,1146,1825,2115):
                    if frame in results:
                        v=results[frame]
                        self.assertEqual(v[2],1,(name,frame,v))
                        self.assertLessEqual(abs(v[7]),25,(name,frame,v))

    def test_angled_cross_preserves_the_incoming_corridor(self):
        result=self.sequence('cross_angled')[-1]
        self.assertEqual(result[0],2,'斜向接近的双侧宽开口应识别为十字')
        self.assertEqual(result[2],1)
        self.assertLess(result[6],0)
        self.assertLess(result[7],0,'横向支路不能替代当前前向走廊')


    def test_inside_requires_a_visible_turn_toward_the_island(self):
        self.lib.reset();low=self.fork_image(79);high=self.fork_image(96)
        for ms,mm in ((0,0),(20,5),(40,10)):self.feed(low,ms,mm)
        for ms,mm in ((60,60),(80,100),(100,150)):result=self.feed(high,ms,mm)
        self.assertEqual(result[0],4)
        image=bytearray([20])*(188*120)
        for row in range(30,117):
            width=44+(row-30)*123//86;center=93+30-(116-row)*15//86
            left=max(0,center-width//2);right=min(187,center+width//2)
            image[row*188+left:row*188+right+1]=bytes([220])*(right-left+1)
        for ms,mm in ((120,320),(140,340),(160,360)):result=self.feed(image,ms,mm)
        self.assertEqual(result[0],4,'仅曲率差同号、但前向路径仍在反侧，不能提交环内')
        self.assertGreater(result[7],0,'可见双边走廊不能被固定入环偏置强制推向黑区')

    def frozen_frame(self,name):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/element-sequences.zip') as archive:
            return bytearray(archive.read(name+'.gray')[:188*120])

    def test_confirmed_cross_can_bridge_a_visible_wide_opening(self):
        self.sequence('cross_angled');image=self.frozen_frame('cross_white')
        result=self.feed(image,80,12420)
        self.assertEqual(result[2],1,'已确认十字的可见白区和实测锚点支持短距离通过')
        image[30*188:76*188]=bytes([20])*(46*188)
        self.assertEqual(self.feed(image,100,12423)[2],0,'黑色远场不能被过渡处理掩盖')


    def test_confirmed_entry_keeps_visible_branch_when_edges_spread(self):
        self.lib.reset()
        def mirror(image):
            return bytearray().join(image[row*188:(row+1)*188][::-1] for row in range(120))
        low=mirror(self.fork_image(79));high=mirror(self.fork_image(96))
        for ms,mm in ((0,0),(20,5),(40,10)):self.feed(low,ms,mm)
        for ms,mm in ((60,60),(80,100),(100,150)):result=self.feed(high,ms,mm)
        self.assertEqual(result[0],4)
        result=self.feed(self.frozen_frame('ring_white'),120,160)
        self.assertEqual(result[2],1,'环岛入口宽白区必须依据实际锚点支持有限通行')
        self.assertGreater(result[7],0)

    def test_cross_direction_is_not_replaced_by_a_side_arm_edge(self):
        result=self.sequence('cross_direction_change')[-1]
        self.assertEqual(result[0],2)
        self.assertEqual(result[2],1,'通过十字过程中应连续跟踪实际可见的前向出口')
        self.assertLessEqual(result[7],0,'横向支路的单条边不能把左前方出口改成右转')

    def test_current_turn_is_not_promoted_to_a_distant_ring(self):
        self.assertFalse(any(r[0]>=3 for r in self.sequence('turning_approach')),
                         '近场仍有明显转弯偏差时不把远处分叉提交为本车入口')

    def test_lower_reverse_merging_branch_is_not_a_ring_entry(self):
        self.assertFalse(any(r[0]>=4 for r in self.sequence('reverse_merge')),
                         '远端灰岛可预告，但近端直边的反向汇入口不能正式入环')

    def test_cross_releases_when_near_exit_closes_before_a_curve(self):
        result=self.sequence('cross_exit_curve')
        self.assertIn(2,{r[0] for r in result})
        self.assertNotEqual(result[-1][0],2,'十字已通过、近场双边恢复时不能被下一弯道卡住')

    def test_inside_visible_outer_curve_keeps_original_coverage_contract(self):
        result=self.sequence('ring_inside_path')[-1]
        self.assertEqual(result[0],5)
        self.assertEqual(result[2],1,'环内连续实测外边覆盖近远控制窗口时应继续跟踪')

    def test_ring_exit_uses_the_new_outer_gap_before_it_leaves_view(self):
        phases={r[0] for r in self.sequence('ring_exit')}
        self.assertNotIn(6,phases,'远处出口只作预告，缺口未到近场不得提前出环')

    def test_ring_exit_completes_after_the_opening_reaches_near_field(self):
        result=self.sequence('ring_exit_complete')
        self.assertIn(6,{r[0] for r in result},'出口到达近场后应完成出环')
        self.assertLess(result[-1][0],3,'双边恢复后必须结束环岛状态')


    def test_left_ring_follows_the_island_when_outer_far_closure_is_hidden(self):
        result=self.sequence('left_ring_exit_preview')[-1]
        self.assertEqual(result[0],5)
        self.assertLess(result[7],0,'外側出口远端闭合点出画面时仍需保持环内方向')

    def test_exit_memory_does_not_replace_a_current_measured_inner_edge(self):
        self.sequence('ring_exit_complete',1100)
        image=bytearray([20])*(188*120)
        for row in range(30,117):
            half=(44+(row-30)*123//86)//2
            image[row*188+93-half:row*188+94+half]=bytes([220])*(half*2+1)
        result=self.feed(image,22000,3000)
        self.assertLessEqual(abs(result[6]),2)
        self.assertLessEqual(abs(result[7]),2,'当前实测的闭合走廊优先于历史图像路径')

    def test_ring_exit_can_close_into_a_curve_without_a_full_straight_frame(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/element-sequences.zip') as archive:
            meta=json.loads(archive.read('metadata.json'));info=meta['segments']['ring_exit_complete']
            data=bytearray(archive.read(info['file']))
        self.lib.reset();values=(C.c_int*18)();entry=None
        for i in range(info['frames']):
            distance=info['distances_mm'][i] if 'distances_mm' in info else info['distance_start_mm']+i*info['step_mm']
            pixels=(C.c_ubyte*(188*120)).from_buffer(data,i*188*120)
            self.lib.frame(pixels,i*meta['frame_ms'],distance,values)
            if values[0]==6:entry=(i*meta['frame_ms'],distance);break
        self.assertIsNotNone(entry)
        image=bytearray([20])*(188*120)
        # 117..119也在相机中；不能在ROI末端人工添一条黑带。
        for row in range(30,120):
            width=44+(row-30)*123//86
            center=93+int(18*(116-row)/86)
            left=max(0,center-width//2);right=min(187,center+width//2)
            image[row*188+left:row*188+right+1]=bytes([220])*(right-left+1)
        for step in range(1,5):result=self.feed(image,entry[0]+step*20,entry[1]+180+step*5)
        self.assertEqual(result[2],1)
        self.assertLess(result[0],3,'出口已闭合且满足出环里程时不能要求整幅直道才能退出')
        self.assertFalse(result[4]&1)


    def test_r50a45_frozen_two_direction_sequences_do_not_confirm_elements(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/r50a45-sequences.zip') as archive:
            meta=json.loads(archive.read('metadata.json'))
            for name,info in meta['segments'].items():
                with self.subTest(case=name):
                    self.lib.reset();data=bytearray(archive.read(info['file']));values=(C.c_int*18)()
                    for i,sample in enumerate(info['samples']):
                        frame=(C.c_ubyte*(188*120)).from_buffer(data,i*188*120)
                        self.lib.frame(frame,sample['time_ms'],sample['distance_mm'],values)
                        self.assertLess(values[0],2,'普通八连弯不得确认十字或环岛')
                        self.assertFalse(values[4]&1,'普通八连弯不得产生元素超时')


if __name__=='__main__':unittest.main(verbosity=2)
