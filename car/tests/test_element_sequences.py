"""以冻结仿真灰度序列复现普通弯道误判、真实分叉入环，使用实际C接口。"""
import ctypes as C
import json
import os
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
 v[0]=output.element;v[1]=output.side;v[2]=output.valid;
 v[3]=output.confidence;v[4]=output.fault;v[5]=output.curve_outer_supported;
 v[6]=output.near_error_px;v[7]=output.far_error_px;
 v[8]=output.curve_outer_side;
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
        self.lib.reset();result=[];values=(C.c_int*9)()
        for i in range(min(info['frames'],max_frames or info['frames'])):
            frame=(C.c_ubyte*(188*120)).from_buffer(data,i*188*120)
            distance=info['distances_mm'][i] if 'distances_mm' in info else info['distance_start_mm']+i*info['step_mm']
            self.lib.frame(frame,i*meta['frame_ms'],distance,values)
            result.append(tuple(values))
        return result

    def test_ordinary_curves_do_not_latch_roundabout(self):
        for scene in ('ordinary_entry','ordinary_u','ordinary_s'):
            with self.subTest(scene=scene):
                result=self.sequence(scene)
                self.assertFalse(any(v[0]>=3 for v in result),'普通弯道被标为环岛')
                self.assertFalse(any(v[0]==2 for v in result),'普通弯道被标为十字')
                self.assertFalse(any(v[4]&1 for v in result),'普通弯道触发元素状态超时')

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
        self.assertGreaterEqual(result[3],40)
        self.assertEqual(result[5],1,'运行中的普通弯道应使用连续外边支持')

    def test_quality_uses_measured_support_in_control_windows(self):
        result=self.sequence('curve_quality')[-1]
        self.assertEqual(result[5],1,'近远窗口仍有经过连续性检查的实测外边')
        self.assertGreaterEqual(result[3],40,'窗口外不可见区域不应否定充分的控制预瞄证据')

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
        values=(C.c_int*9)();pixels=(C.c_ubyte*len(image)).from_buffer(image)
        self.lib.frame(pixels,ms,mm,values)
        return tuple(values)

    def test_approach_limit_precedes_entry_commit(self):
        low=self.fork_image(79);high=self.fork_image(96)
        for boundary in ('distance','time'):
            with self.subTest(boundary=boundary):
                self.lib.reset()
                for ms,mm in ((0,0),(20,5),(40,10)):result=self.feed(low,ms,mm)
                self.assertEqual(result[0],3)
                if boundary=='distance':
                    self.feed(low,60,690);samples=((80,695),(100,705),(120,715))
                else:
                    self.feed(low,9940,20);samples=((9960,25),(10020,30),(10060,35))
                phases=[self.feed(high,ms,mm)[0] for ms,mm in samples]
                self.assertNotIn(4,phases,'过期候选不能在确认提交时越过限界')

    def test_black_forward_branch_cannot_commit_entry(self):
        self.lib.reset();low=self.fork_image(79);hidden=self.fork_image(96,True)
        for ms in (0,20,40):result=self.feed(low,ms,ms//4)
        self.assertEqual(result[0],3)
        phases=[self.feed(hidden,ms,ms//4)[0] for ms in (60,80,100)]
        self.assertNotIn(4,phases,'旧分叉证据不能为全黑远场提交入环')

    def test_missing_frame_breaks_continuous_candidate_confirmation(self):
        self.lib.reset();image=self.fork_image(79);values=(C.c_int*9)()
        self.feed(image,0,0);self.feed(image,20,5)
        self.lib.frame(None,40,10,values)
        self.assertNotEqual(self.feed(image,60,15)[0],3,'缺失帧不能把不连续证据凑成三帧候选')

    def test_angled_cross_preserves_the_incoming_corridor(self):
        result=self.sequence('cross_angled')[-1]
        self.assertEqual(result[0],2,'斜向接近的双侧宽开口应识别为十字')
        self.assertEqual(result[2],1)
        self.assertLess(result[6],0)
        self.assertLess(result[7],0,'横向支路不能替代当前前向走廊')

    def test_far_branch_does_not_switch_to_an_unsupported_outer_side(self):
        self.sequence('curve_quality')
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/element-sequences.zip') as archive:
            image=bytearray(archive.read('cross_angled.gray')[:188*120])
        result=self.feed(image,80,1603)
        self.assertEqual(result[8],2,'新外侧缺少近场证据时保持当前连续实测右外边')
        self.assertEqual(result[5],1)

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
        self.assertGreaterEqual(result[3],35)
        image[30*188:76*188]=bytes([20])*(46*188)
        self.assertEqual(self.feed(image,100,12423)[2],0,'黑色远场不能被过渡处理掩盖')

    def test_wide_opening_bridge_has_distance_and_time_limits(self):
        image=self.frozen_frame('cross_white')
        for boundary in ('distance','time'):
            with self.subTest(boundary=boundary):
                self.sequence('cross_angled')
                self.assertEqual(self.feed(image,80,12420)[2],1)
                result=self.feed(image,100 if boundary=='distance' else 6200,
                                 13121 if boundary=='distance' else 12423)
                self.assertEqual(result[2],0,'白区过渡不能无限延续')

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
        self.assertFalse(any(r[0]>=3 for r in self.sequence('reverse_merge')),
                         '远端灰岛、近端直边的反向汇入口不能作为环岛入口')

    def test_cross_releases_when_near_exit_closes_before_a_curve(self):
        result=self.sequence('cross_exit_curve')
        self.assertIn(2,{r[0] for r in result})
        self.assertNotEqual(result[-1][0],2,'十字已通过、近场双边恢复时不能被下一弯道卡住')
        self.assertFalse(any(r[4]&1 for r in result))

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

    def test_old_exit_hint_cannot_authorize_a_later_opening(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/element-sequences.zip') as archive:
            data=archive.read('ring_exit_complete.gray')
        near_exit=bytearray(data[1200*188*120:1201*188*120])
        for boundary in ('time','distance'):
            with self.subTest(boundary=boundary):
                self.assertEqual(self.sequence('ring_exit_complete',1100)[-1][0],5)
                results=[self.feed(near_exit,32000+i*20 if boundary=='time' else 22000+i*20,
                                   3100+i*3 if boundary=='time' else 4000+i*3) for i in range(3)]
                self.assertFalse(any(r[0]==6 for r in results),'失效的出口预告不能为后来的开口授权出环')

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


if __name__=='__main__':unittest.main(verbosity=2)
