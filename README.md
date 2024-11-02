h264qp by HellGauss (HG)

********************************

USAGE:
1) h264qp.exe inputFile
2) h264qp.exe inputFile outputStatsFile

This script displays on stdout general QP statistics for h264 video in inputFile
Optionally, it writes detailed statistics for each frame in outputStatsFile
Log messages are on stderr
Requires ffmpeg.exe and ffprobe.exe in same dir or PATH (tested with ffmpeg 7.1 on Windows 10)

********************************

Output format for general statistics (stdout):
{Frametype}: This select the group of frames considered
{N} = Number of frames in the considered group
{N/TOT} = % of N over total number frames
{Total Size} = Sum of size in bytes of frames in the considered group, as measured by ffprobe in frmae->pkt_size
{Total Size/TOT} = % Total size of frame in group over total size of every frame
{Average Size} = {Total Size} / {N}
{AVERAGE QP} = Average QP of all macroblocks in the group. If video is 10 bit, -12 is added to this value.
{StdDev (frames)} = Standard deviation of the average QP of each frames
{StdDev (MB)} = Standard deviation of all macroblocks in all frames in the group

At the end, statistics on consecutive BFrames groups are reported.

********************************

Output format for per-frame statistics (outputStatsFile):
First row is number of frames. Next rows are info on each frame
{TYPE} {SIZE} {POSITION} {AVG QP} {STDEV QP-MB}
Values are separated by tab.

{TYPE} = K for keyframes. If not a key frame, as reported in pict_type by ffprobe
{SIZE} = pkt_size in FRAME field by ffprobe
{POSITION} = pkt_pos in FRAME field by ffprobe
{AVG QP} = Average QP of each macroblock in the frame, as calculated from -debug qp. If video is 10 bit, -12 is added to this value.
{STDEV QP-MB}: Standard deviation of MacroBlocks QP values in the frame

********************************

TECH

The script launches the command

ffmpeg -hide_banner -loglevel error -threads 1 -i inputFile -map 0:V:0 -c:v copy -f h264 pipe: | ffprobe -threads 1 -v quiet -show_frames -show_streams -show_entries frame=key_frame,pkt_pos,pkt_size,pict_type -debug qp -i pipe:

and parses the outputs in stdout and stderr.

********************************

NOTES:
1) For fast use, drag and drop video file in DragNDropHere.bat. A .txt file with general statistic and a .dat file with per frame statistic will be generated in the same dir as the input
2) Progress LOG messages are reported each 1000 frames analyzed
3) For 8bit x264 encoding, QP analysis coincides with the encoding log of x264. For 10bit QP are shifted by -12. Average frame size can be a little different. Consecutive BFrames are calculated in a different way than x264 log. 
