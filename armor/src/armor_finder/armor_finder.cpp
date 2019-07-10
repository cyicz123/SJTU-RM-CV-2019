//
// Created by xinyang on 19-3-27.
//

/*===========================================================================*/
/*                               使用本代码的兵种                               */
/*===========================================================================*/
/*   _______________   _______________   _______________   _______________   */
/*  |     _____     | |     _  _      | |     ____      | |     _____     |  */
/*  ||   |___ /    || ||   | || |    || ||   | ___|    || ||   |___  |   ||  */
/*  ||     |_ \    || ||   | || |_   || ||   |___ \    || ||      / /    ||  */
/*  ||    ___) |   || ||   |__   _|  || ||    ___) |   || ||     / /     ||  */
/*  ||   |____/    || ||      |_|    || ||   |____/    || ||    /_/      ||  */
/*  |_______________| |_______________| |_______________| |_______________|  */
/*                                                                           */
/*===========================================================================*/

#define LOG_LEVEL LOG_NONE
#include <log.h>
#include <options/options.h>
#include <show_images/show_images.h>
#include <opencv2/highgui.hpp>
#include <armor_finder/armor_finder.h>
#include <sys/time.h>

std::map<int, string> id2name = {                               //装甲板id到名称的map
        {-1, "OO"},{ 0, "NO"},
        { 1, "B1"},{ 2, "B2"},{ 3, "B3"},{ 4, "B4"},{ 5, "B5"},{ 6, "B7"},{ 7, "B8"},
        { 8, "R1"},{ 9, "R2"},{10, "R3"},{11, "R4"},{12, "R5"},{13, "R7"},{14, "R8"},
};

std::map<string, int> name2id = {                               //装甲板名称到id的map
        {"OO", -1},{"NO",  0},
        {"B1",  1},{"B2",  2},{"B3",  3},{"B4",  4},{"B5",  5},{"B7",  6},{"B8",  7},
        {"R1",  8},{"R2",  9},{"R3", 10},{"R4", 11},{"R5", 12},{"R7", 13},{"R8", 14},
};

ArmorFinder::ArmorFinder(uint8_t &color, Serial &u, const string &paras_folder, const uint8_t &use) :
        serial(u),
        enemy_color(color),
        state(STANDBY_STATE),
        classifier(paras_folder),
        contour_area(0),
        use_classifier(use),
        boxid(-1),
        tracking_cnt(0) {
}

void ArmorFinder::run(cv::Mat &src) {
    src_raw = src;
    cv::Mat src_use = src.clone();      // 实际参与计算的图像对象

    if (show_armor_box) {                 // 根据条件显示当前目标装甲板
        showArmorBox("box", src, armor_box, boxid);
        cv::waitKey(1);
    }
//    stateSearchingTarget(src_use);                    // for debug
//    return;
    switch (state) {
        case SEARCHING_STATE:
            if (stateSearchingTarget(src_use)) {
                if ((armor_box & cv::Rect2d(0, 0, 640, 480)) == armor_box) { // 判断装甲板区域是否脱离图像区域
                    if (!classifier || !use_classifier) {                    /* 如果分类器不可用或者不使用分类器 */
                        cv::Mat roi = src_use.clone()(armor_box), roi_gray;  /* 就使用装甲区域亮点数判断是否跟丢 */
                        cv::cvtColor(roi, roi_gray, CV_RGB2GRAY);
                        cv::threshold(roi_gray, roi_gray, 180, 255, cv::THRESH_BINARY);
                        contour_area = cv::countNonZero(roi_gray);
                    }
                    tracker = TrackerToUse::create();                       // 成功搜寻到装甲板，创建tracker对象
                    tracker->init(src_use, armor_box);
                    state = TRACKING_STATE;
                    tracking_cnt = 0;
                    LOGM(STR_CTR(WORD_LIGHT_CYAN, "into track"));
                }
            }
            break;
        case TRACKING_STATE:
            if (!stateTrackingTarget(src_use) || ++tracking_cnt > 100) {    // 最多追踪100帧图像
                state = SEARCHING_STATE;
                LOGM(STR_CTR(WORD_LIGHT_YELLOW, "into search!"));
            }
            break;
        case STANDBY_STATE:
        default:
            stateStandBy();
    }
}

bool sendTarget(Serial &serial, double x, double y, double z) {
    static short x_tmp, y_tmp, z_tmp;
    static time_t last_time = time(nullptr);
    static int fps;
    uint8_t buff[8];

    time_t t = time(nullptr);
    if (last_time != t) {
        last_time = t;
        cout << "fps:" << fps << ", (" << x << "," << y << "," << z << ")" << endl;
        fps = 0;
    }
    fps += 1;

    x_tmp = static_cast<short>(x * (32768 - 1) / 100);
    y_tmp = static_cast<short>(y * (32768 - 1) / 100);
    z_tmp = static_cast<short>(z * (32768 - 1) / 1000);

    buff[0] = 's';
    buff[1] = static_cast<char>((x_tmp >> 8) & 0xFF);
    buff[2] = static_cast<char>((x_tmp >> 0) & 0xFF);
    buff[3] = static_cast<char>((y_tmp >> 8) & 0xFF);
    buff[4] = static_cast<char>((y_tmp >> 0) & 0xFF);
    buff[5] = static_cast<char>((z_tmp >> 8) & 0xFF);
    buff[6] = static_cast<char>((z_tmp >> 0) & 0xFF);
    buff[7] = 'e';

    return serial.WriteData(buff, sizeof(buff));
}

bool ArmorFinder::sendBoxPosition() {
    auto rect = armor_box;
    double dx = rect.x + rect.width / 2 - 320;
    double dy = rect.y + rect.height / 2 - 240 - 20;
    double yaw = atan(dx / FOCUS_PIXAL) * 180 / PI;
    double pitch = atan(dy / FOCUS_PIXAL) * 180 / PI;
    double dist = DISTANCE_HEIGHT / armor_box.height;
//    cout << yaw << endl;
    return sendTarget(serial, yaw, -pitch, dist);
}