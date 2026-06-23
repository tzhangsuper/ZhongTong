/*
 * Copyright (c) 2016-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <poll.h>
#include <math.h>

#include <string.h>
#include <linux/videodev2.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "camera_v4l2_opencv.h"

static bool quit = false;

using namespace std;

long toEpochOffset_ms;
/*
* 系统中的某些时间戳是 monotonic 类型（稳定，不会跳变)
* 想打印真实的 epoch 时间（可读性高）
*/ 
long getEpochTimeShift(){
	struct timeval  epochtime;
	struct timespec vsTime;
	struct timespec realTime;

	gettimeofday(&epochtime, NULL);
	clock_gettime(CLOCK_MONOTONIC, &vsTime);
	clock_gettime(CLOCK_REALTIME, &realTime);                                                                                                                                                                   

	long uptime_ms = vsTime.tv_sec* 1000 + (long)  round( vsTime.tv_nsec/ 1000000.0);
	long epoch_ms =  epochtime.tv_sec * 1000  + (long) round( epochtime.tv_usec/1000.0);
	long x_ms =  realTime.tv_sec* 1000 + (long)  round( realTime.tv_nsec/ 1000000.0);
	
	printf("toEpochOffset_ms:%ld, toRealMonoOffset-ms: %ld\n", epoch_ms - uptime_ms, x_ms - uptime_ms);
	return epoch_ms - uptime_ms;
}

static void
print_usage(void)
{
    printf("\n\tUsage: camera_v4l2_cuda [OPTIONS]\n\n"
           "\tExample: \n"
           "\t./camera_v4l2_opencv -d /dev/video0 -s 1920x1080 -f UYVY -n 30 \n\n"
           "\tSupported options:\n"
           "\t-d\t\tSet V4l2 video device node\n"
           "\t-s\t\tSet output resolution of video device\n"
           "\t-f\t\tSet output pixel format of video device (supports only YUYV/YVYU/UYVY/VYUY/GREY/MJPEG)\n"
           "\t-r\t\tSet renderer frame rate (30 fps by default)\n"
           "\t-n\t\tSave the n-th frame before VIC processing\n"
           "\t-v\t\tEnable verbose message\n"
           "\t-h\t\tPrint this usage\n\n"
           "\tNOTE: It runs infinitely until you terminate it with <ctrl+c>\n");
}

/*
* 解析终端命令行 
* ./camera_demo -d /dev/video0 -s 1280x720 -f YUYV -r 30 -n 10 -v
*/ 
static bool
parse_cmdline(context_t * ctx, int argc, char **argv)
{
    int c;

    if (argc < 2)
    {
        print_usage();
        exit(EXIT_SUCCESS);
    }

    while ((c = getopt(argc, argv, "d:s:f:r:n:cvh")) != -1)
    {
        switch (c)
        {
            case 'd':
                ctx->cam_devname = optarg;
                break;
            case 's':
                if (sscanf(optarg, "%dx%d",
                            &ctx->cam_w, &ctx->cam_h) != 2)
                {
                    print_usage();
                    return false;
                }
                break;
            case 'f':
                if (strcmp(optarg, "YUYV") == 0)
                    ctx->cam_pixfmt = V4L2_PIX_FMT_YUYV;
                else if (strcmp(optarg, "YVYU") == 0)
                    ctx->cam_pixfmt = V4L2_PIX_FMT_YVYU;
                else if (strcmp(optarg, "VYUY") == 0)
                    ctx->cam_pixfmt = V4L2_PIX_FMT_VYUY;
                else if (strcmp(optarg, "UYVY") == 0)
                    ctx->cam_pixfmt = V4L2_PIX_FMT_UYVY;
                else if (strcmp(optarg, "GREY") == 0)
                    ctx->cam_pixfmt = V4L2_PIX_FMT_GREY;
				else if(!strcmp(optarg, "ABGR32") == 0)
					ctx->cam_pixfmt = V4L2_PIX_FMT_ABGR32;
                else if (strcmp(optarg, "MJPEG") == 0)
                    ctx->cam_pixfmt = V4L2_PIX_FMT_MJPEG;
                else
                {
                    print_usage();
                    return false;
                }
                sprintf(ctx->cam_file, "camera.%s", optarg);
                break;
            case 'r':
                ctx->fps = strtol(optarg, NULL, 10); // 转换成整型数据
                break;
            case 'n':
                ctx->save_n_frame = strtol(optarg, NULL, 10);
                break;
            case 'v':
                ctx->enable_verbose = true;
                break;
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
                break;
            default:
                print_usage();
                return false;
        }
    }

    return true;
}

static void
set_defaults(context_t * ctx)
{
    memset(ctx, 0, sizeof(context_t));

    ctx->cam_devname = "/dev/video1";
    ctx->cam_fd = -1;
    ctx->cam_pixfmt = V4L2_PIX_FMT_UYVY;
    ctx->cam_w = 1920;
    ctx->cam_h = 1080;
    ctx->frame = 0;
    ctx->save_n_frame = 1;

    ctx->g_buff = NULL;
    ctx->capture_dmabuf = false;  // opencv display v4l2 can't be true
    ctx->fps = 30;

    ctx->enable_verbose = false;
}

static bool
save_frame_to_file(context_t * ctx, struct v4l2_buffer * buf)
{
    int file;

    file = open(ctx->cam_file, O_CREAT | O_WRONLY | O_APPEND | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    if (-1 == file)
        ERROR_RETURN("Failed to open file for frame saving");

    if (-1 == write(file, ctx->g_buff[buf->index].start,
                ctx->g_buff[buf->index].size))
    {
        close(file);
        ERROR_RETURN("Failed to write frame into file");
    }

    close(file);

    return true;
}

static bool
camera_initialize(context_t * ctx)
{
    struct v4l2_format fmt;

    /* Open camera device */
    ctx->cam_fd = open(ctx->cam_devname, O_RDWR);
    if (ctx->cam_fd == -1)
        ERROR_RETURN("Failed to open camera device %s: %s (%d)",
                ctx->cam_devname, strerror(errno), errno);

    /* Set camera output format */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = ctx->cam_w;
    fmt.fmt.pix.height = ctx->cam_h;
    fmt.fmt.pix.pixelformat = ctx->cam_pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    if (ioctl(ctx->cam_fd, VIDIOC_S_FMT, &fmt) < 0)
        ERROR_RETURN("Failed to set camera output format: %s (%d)",
                strerror(errno), errno);

    /* Get the real format in case the desired is not supported */
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->cam_fd, VIDIOC_G_FMT, &fmt) < 0)
        ERROR_RETURN("Failed to get camera output format: %s (%d)",
                strerror(errno), errno);
    if (fmt.fmt.pix.width != ctx->cam_w ||
            fmt.fmt.pix.height != ctx->cam_h ||
            fmt.fmt.pix.pixelformat != ctx->cam_pixfmt)
    {
        WARN("The desired format is not supported");
        ctx->cam_w = fmt.fmt.pix.width;
        ctx->cam_h = fmt.fmt.pix.height;
        ctx->cam_pixfmt =fmt.fmt.pix.pixelformat;
    }

    struct v4l2_streamparm streamparm;
    memset (&streamparm, 0x00, sizeof (struct v4l2_streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl (ctx->cam_fd, VIDIOC_G_PARM, &streamparm);

    INFO("Camera ouput format: (%d x %d)  stride: %d, imagesize: %d, frate: %u / %u",
            fmt.fmt.pix.width,
            fmt.fmt.pix.height,
            fmt.fmt.pix.bytesperline,
            fmt.fmt.pix.sizeimage,
            streamparm.parm.capture.timeperframe.denominator,
            streamparm.parm.capture.timeperframe.numerator);

    return true;
}

static bool
init_components(context_t * ctx)
{
    if (!camera_initialize(ctx))
        ERROR_RETURN("Failed to initialize camera device");
    INFO("Initialize v4l2 components successfully");
    return true;
}

static bool
request_camera_buff(context_t *ctx)
{
    /* Request camera v4l2 buffer */
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof(rb));
    rb.count = V4L2_BUFFERS_NUM;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(ctx->cam_fd, VIDIOC_REQBUFS, &rb) < 0) // 驱动内部创建 N 个 buffer
        ERROR_RETURN("Failed to request v4l2 buffers: %s (%d)",
                strerror(errno), errno);
    if (rb.count != V4L2_BUFFERS_NUM)
        ERROR_RETURN("V4l2 buffer number is not as desired");

    for (unsigned int index = 0; index < V4L2_BUFFERS_NUM; index++)
    {
        struct v4l2_buffer buf;

        /* Query camera v4l2 buf length */
        memset(&buf, 0, sizeof buf);
        buf.index = index;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;

        if (ioctl(ctx->cam_fd, VIDIOC_QUERYBUF, &buf) < 0) // 获取 index=i 的缓冲区地址、长度
            ERROR_RETURN("Failed to query buff: %s (%d)",
                    strerror(errno), errno);

        /* TODO: add support for multi-planer
           Enqueue empty v4l2 buff into camera capture plane */
        buf.m.fd = (unsigned long)ctx->g_buff[index].dmabuff_fd;
        if (buf.length != ctx->g_buff[index].size)
        {
            WARN("Camera v4l2 buf length is not expected");
            ctx->g_buff[index].size = buf.length;
        }

        if (ioctl(ctx->cam_fd, VIDIOC_QBUF, &buf) < 0)
            ERROR_RETURN("Failed to enqueue buffers: %s (%d)",
                    strerror(errno), errno);
    }

    return true;
}

static bool
request_camera_buff_mmap(context_t *ctx)
{
    /* Request camera v4l2 buffer */
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof(rb));
    rb.count = V4L2_BUFFERS_NUM;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    if (ioctl(ctx->cam_fd, VIDIOC_REQBUFS, &rb) < 0)
        ERROR_RETURN("Failed to request v4l2 buffers: %s (%d)",
                strerror(errno), errno);
    if (rb.count != V4L2_BUFFERS_NUM)
        ERROR_RETURN("V4l2 buffer number is not as desired");

    for (unsigned int index = 0; index < V4L2_BUFFERS_NUM; index++)
    {
        struct v4l2_buffer buf;

        /* Query camera v4l2 buf length */
        memset(&buf, 0, sizeof buf);
        buf.index = index;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(ctx->cam_fd, VIDIOC_QUERYBUF, &buf) < 0)
            ERROR_RETURN("Failed to query buff: %s (%d)",
                    strerror(errno), errno);

        ctx->g_buff[index].size = buf.length;
        ctx->g_buff[index].start = (unsigned char *)
            mmap (NULL /* start anywhere */,
                    buf.length,
                    PROT_READ | PROT_WRITE /* required */,
                    MAP_SHARED /* recommended */,
                    ctx->cam_fd, buf.m.offset);
        if (MAP_FAILED == ctx->g_buff[index].start)
            ERROR_RETURN("Failed to map buffers");

        if (ioctl(ctx->cam_fd, VIDIOC_QBUF, &buf) < 0)
            ERROR_RETURN("Failed to enqueue buffers: %s (%d)",
                    strerror(errno), errno);
    }

    return true;
}

static bool
prepare_buffers(context_t * ctx)
{

    /* Allocate global buffer context */
    ctx->g_buff = (nv_buffer *)malloc(V4L2_BUFFERS_NUM * sizeof(nv_buffer));
    if (ctx->g_buff == NULL)
        ERROR_RETURN("Failed to allocate global buffer context");

    if (ctx->capture_dmabuf) {
        if (!request_camera_buff(ctx))
            ERROR_RETURN("Failed to set up camera buff");
    } else {
        if (!request_camera_buff_mmap(ctx))
            ERROR_RETURN("Failed to set up camera buff");
    }

    INFO("Succeed in preparing stream buffers");
    return true;
}

static bool
start_stream(context_t * ctx)
{
    enum v4l2_buf_type type;

    /* Start v4l2 streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->cam_fd, VIDIOC_STREAMON, &type) < 0)
        ERROR_RETURN("Failed to start streaming: %s (%d)",
                strerror(errno), errno);

    usleep(200);

    INFO("Camera video streaming on ...");
    return true;
}

static void
signal_handle(int signum)
{
    printf("Quit due to exit command from user!\n");
    quit = true;
}

static bool
start_capture(context_t * ctx)
{
    struct sigaction sig_action;
    struct pollfd fds[1];

    /* Register a shuwdown handler to ensure
       a clean shutdown if user types <ctrl+c> */
    sig_action.sa_handler = signal_handle;
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_flags = 0;
    sigaction(SIGINT, &sig_action, NULL);


    fds[0].fd = ctx->cam_fd;
    fds[0].events = POLLIN;
    /* Wait for camera event with timeout = 5000 ms */
    while (poll(fds, 1, 5000) > 0 && !quit)
    {
        if (fds[0].revents & POLLIN) {
            struct v4l2_buffer v4l2_buf;

            /* Dequeue a camera buff */
            memset(&v4l2_buf, 0, sizeof(v4l2_buf));
            v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ctx->capture_dmabuf)
                v4l2_buf.memory = V4L2_MEMORY_DMABUF;
            else
                v4l2_buf.memory = V4L2_MEMORY_MMAP;
			
            if (ioctl(ctx->cam_fd, VIDIOC_DQBUF, &v4l2_buf) < 0)
                ERROR_RETURN("Failed to dequeue camera buff: %s (%d)",
                        strerror(errno), errno);
			// Get timestamp from v4l2_buf 
			printf("tv_sec=%ld, tv_usec=%ld\n",
			                 v4l2_buf.timestamp.tv_sec, v4l2_buf.timestamp.tv_usec);
			long temp_ms = 1000*v4l2_buf.timestamp.tv_sec + (long)round(v4l2_buf.timestamp.tv_usec / 1000.0); // 系统运行时间
			long epochTimeStamp_ms = temp_ms + toEpochOffset_ms ;
			printf( "the frame's timestamp in epoch ms is: %ld\n", epochTimeStamp_ms);
			
			struct timeval tv;
			gettimeofday(&tv, 0);
			printf("current time ms: %ld, %ld\n", tv.tv_sec, tv.tv_usec);
			
            ctx->frame++;
            /* Save the n-th frame to file */
            if (ctx->frame == ctx->save_n_frame)
                save_frame_to_file(ctx, &v4l2_buf);
				
            printf("frame No : %d\n", ctx->frame);
			cv::Size szSize(ctx->cam_w, ctx->cam_h);
			if(ctx->cam_pixfmt == V4L2_PIX_FMT_ABGR32)
			{
				cv::Mat imgMat(szSize, CV_8UC4, ctx->g_buff[v4l2_buf.index].start);
				cv::imshow("camera 0", imgMat);
            cv::waitKey(1); // Wait for 'esc' key press to exit
			}
			else
			{
				cv::Mat imgMat(szSize, CV_8UC3);
				cv::Mat srcMat(szSize, CV_8UC2, ctx->g_buff[v4l2_buf.index].start);
				cvtColor(srcMat, imgMat, cv::COLOR_YUV2BGR_UYVY);
				cv::imshow("camera 0", imgMat);
				cv::waitKey(1); // Wait for 'esc' key press to exit
			}


            /* Enqueue camera buffer back to driver */
            if (ioctl(ctx->cam_fd, VIDIOC_QBUF, &v4l2_buf))
                ERROR_RETURN("Failed to queue camera buffers: %s (%d)",
                        strerror(errno), errno);
        }
    }
    return true;
}

static bool
stop_stream(context_t * ctx)
{
    enum v4l2_buf_type type;

    /* Stop v4l2 streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->cam_fd, VIDIOC_STREAMOFF, &type))
        ERROR_RETURN("Failed to stop streaming: %s (%d)",
                strerror(errno), errno);

    INFO("Camera video streaming off ...");
    return true;
}

int
main(int argc, char *argv[])
{
    context_t ctx;
    int error = 0;

    set_defaults(&ctx);

    CHECK_ERROR(parse_cmdline(&ctx, argc, argv), cleanup,
            "Invalid options specified");
			
	// Stick this somewhere so that it runs once, on the startup of your capture process
    // noting, if you hibernate a laptop, you might need to recalc this if you don't restart 
    // the process after dehibernation
    toEpochOffset_ms = getEpochTimeShift();
	
    /* Initialize camera and EGL display, EGL Display will be used to map
       the buffer to CUDA buffer for CUDA processing */
    CHECK_ERROR(init_components(&ctx), cleanup,
            "Failed to initialize v4l2 components");

    CHECK_ERROR(prepare_buffers(&ctx), cleanup,
                "Failed to prepare v4l2 buffs");

    CHECK_ERROR(start_stream(&ctx), cleanup,
            "Failed to start streaming");

    CHECK_ERROR(start_capture(&ctx), cleanup,
            "Failed to start capturing")

    CHECK_ERROR(stop_stream(&ctx), cleanup,
            "Failed to stop streaming");

cleanup:
    if (ctx.cam_fd > 0)
        close(ctx.cam_fd);

    if (ctx.g_buff != NULL)
    {
        free(ctx.g_buff);
    }

    if (error)
        printf("App run failed\n");
    else
        printf("App run was successful\n");

    return -error;
}
