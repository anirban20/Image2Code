//
// Created by Ibrahim Radwan on 11/29/17.
//

#include "LineSegmentation.h"

LineSegmentation::LineSegmentation(string path_of_image) {
    this->image_path = path_of_image;

    // Initialize Sieve
    Utilities::sieve();
}

vector<Mat>
LineSegmentation::segment() {
    // Read image and process it
    this->read_image();
    this->pre_process_image();

    return this->divide_image_into_regions();
    /*

    // Find letters contours
    this->find_contours();

    // Divide image into chunks
    this->generate_chunks();

    // Generate initial separator lines using valleys concept
    this->get_initial_lines();
    this->show_lines("out/Initial_Lines.jpg");

    // Get initial regions
    this->generate_regions();

    // Fix collisions
    this->repair_lines();
    this->generate_regions();
    this->show_lines("out/Final_Lines.jpg");


    return this->get_regions();
     */
}

void
LineSegmentation::read_image() {
    this->color_img = imread(this->image_path, CV_LOAD_IMAGE_COLOR);
    this->grey_img = imread(this->image_path, CV_LOAD_IMAGE_GRAYSCALE);
}

void
LineSegmentation::pre_process_image() {
    // ToDo: use Utilities::loadBinaryImage
    // More filters are about to be applied.
    Mat preprocessed_img, smoothed_img;

    // Noise reduction (Currently a basic filter).
    blur(grey_img, smoothed_img, Size(3, 3), Point(-1, -1));

    // OTSU threshold and Binarization.
    threshold(smoothed_img, binary_img, 0.0, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
    imwrite("out/Binary_image.jpg", this->binary_img);
}

vector<Mat>
LineSegmentation::divide_image_into_regions() {
    vector<int> separation_rows;

    // Get empty rows
    int rows = this->binary_img.rows;
    int cols = this->binary_img.cols;

    bool white_counted = false;
    for (int r = 0; r < rows; ++r) {
        bool no_black = true;
        for (int c = 0; c < cols && no_black; ++c) {
            if (this->binary_img.at<uchar>(r, c) < 180) no_black = false;
        }
        if (no_black && !white_counted) {
            white_counted = 1;
            separation_rows.push_back(r);
        } else if (!no_black) {
            white_counted = 0;
        }
    }

    // Store regions
    vector<Mat> regions;
    for (int i = 0; i < separation_rows.size() - 1; ++i) {
        Mat B = this->binary_img(Rect(0, separation_rows[i], cols - 1, separation_rows[i + 1] - separation_rows[i]));
        regions.push_back(B.clone());
        imwrite(LINE_OUTPUT_PATH + to_string(i) + ".jpg",
                B);
    }

    return regions;
}

void
LineSegmentation::find_contours() {
    Mat img_clone = this->binary_img;

    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(img_clone, contours, hierarchy, CV_RETR_LIST, CV_CHAIN_APPROX_NONE, Point(0, 0));

    // Initializing rectangular and poly vectors.
    vector<vector<Point> > contours_poly(contours.size());
    vector<Rect> bound_rect(contours.size() - 1);

    // Getting rectangular boundaries from contours.
    for (size_t i = 0; i < contours.size() - 1; i++) {
        approxPolyDP(Mat(contours[i]), contours_poly[i], 1, true); //apply approximation to polygons with accuracy +-3
        bound_rect[i] = boundingRect(Mat(contours_poly[i]));
    }

    // Merging the rectangular boundaries.
    Rect2d rectangle3;
    vector<Rect> merged_rectangles;
    bool is_repeated;
    Mat drawing = this->color_img.clone();

    // Checking for intersecting rectangles.
    for (int i = 0; i < bound_rect.size(); i++) {
        is_repeated = false;

        for (int j = i + 1; j < bound_rect.size(); j++) {
            rectangle3 = bound_rect[i] & bound_rect[j];

            // Check for intersection/union.
            if ((rectangle3.area() == bound_rect[i].area()) || (rectangle3.area() == bound_rect[j].area())) {
                is_repeated = true;
                rectangle3 = bound_rect[i] | bound_rect[j]; //merging
                Rect2d merged_rectangle(rectangle3.tl().x, rectangle3.tl().y, rectangle3.width, rectangle3.height);

                // Push in merged rectangle after checking all the inner loop.
                if (j == bound_rect.size() - 2)
                    merged_rectangles.push_back(merged_rectangle);

                // Update the current vector.
                bound_rect[j] = merged_rectangle;
            }
        }
        // Adding the non repeated (not intersected) rectangles.
        if (!is_repeated)
            merged_rectangles.push_back(bound_rect[i]);
    }
    // ToDo @Samir55 Remove.
    for (size_t i = 0; i < merged_rectangles.size(); i++)
        rectangle(drawing, merged_rectangles[i].tl(), merged_rectangles[i].br(), TEST_LINE_COLOR, 2, 8, 0);
    imwrite("out/contours.jpg", drawing);
    this->contours = merged_rectangles;
}

void
LineSegmentation::generate_chunks() {
    int width = binary_img.cols;
    chunk_width = width / CHUNKS_NUMBER;

    for (int i_chunk = 0, start_pixel = 0; i_chunk < CHUNKS_NUMBER; ++i_chunk) {
        Chunk *c = new Chunk(i_chunk, start_pixel, chunk_width,
                             Mat(binary_img, Range(0, binary_img.rows), Range(start_pixel, start_pixel + chunk_width)));
        this->chunks.push_back(c);

        imwrite("out/Chunk" + to_string(i_chunk) + ".jpg", this->chunks.back()->img);

        start_pixel += chunk_width;
    }
}

Line *
LineSegmentation::connect_valleys(int i, Valley *current_valley, Line *line, int valleys_min_abs_dist) {
    if (i <= 0 || chunks[i]->valleys.empty()) return line;

    // Choose the closest valley in right chunk to the start valley.
    int connected_to = -1;
    int min_distance = 100000;
    for (int j = 0; j < this->chunks[i]->valleys.size(); j++) {
        Valley *valley = this->chunks[i]->valleys[j];
        // Check if the valley is not connected to any other valley.
        if (valley->used) continue;

        int dist = current_valley->position - valley->position;
        dist = dist < 0 ? -dist : dist;
        if (min_distance > dist && dist <= valleys_min_abs_dist) {
            min_distance = dist, connected_to = j;
        }
    }

    // Return line if the current valley is not connected any more to a new valley in the current chunk of index i.
    if (connected_to == -1) {
        return line;
    }

    line->valleys_ids.push_back(this->chunks[i]->valleys[connected_to]->valley_id);
    Valley *v = this->chunks[i]->valleys[connected_to];
    v->used = true;
    return connect_valleys(i - 1, v, line, valleys_min_abs_dist);
}

void
LineSegmentation::get_initial_lines() {
    // First determine how many chunks to process
    int total_avg_height = 0, number_of_heights = 0;
    for (int i = 0; i < this->chunks.size(); ++i) {
        total_avg_height += this->chunks[i]->avg_height;
        if (this->chunks[i]->avg_height)
            number_of_heights++;
    }
    total_avg_height /= number_of_heights;

    if (total_avg_height < 60)
        this->CHUNCKS_TO_BE_PROCESSED = 19;
    else
        this->CHUNCKS_TO_BE_PROCESSED = 7;

    // Get the histogram of the first CHUNKS_TO_BE_PROCESSED and get the overall average line height.
    int number_of_spaces = 0, valleys_min_abs_dist = 0, total_space_heights = 0;
    number_of_heights = 0;
    for (int i = 0; i < this->CHUNCKS_TO_BE_PROCESSED; i++) {
        int avg_height = this->chunks[i]->find_peaks_valleys(map_valley);
        if (avg_height) number_of_heights++;
        if (this->chunks[i]->avg_white_height) number_of_spaces++;

        valleys_min_abs_dist += avg_height;
        total_space_heights += this->chunks[i]->avg_white_height;
    }
    valleys_min_abs_dist /= number_of_heights;
    this->avg_line_height = valleys_min_abs_dist;
    this->avg_space_height = total_space_heights / number_of_spaces;

    vector<Line *> single_valley;
    // Start form the CHUNKS_TO_BE_PROCESSED chunk.
    for (int i = this->CHUNCKS_TO_BE_PROCESSED - 1; i >= 0; i--) {
        if ((chunks[i]->valleys).empty()) continue;

        // Connect each valley with the nearest ones in the left chunks.
        for (auto valley : chunks[i]->valleys) {
            if (valley->used) continue;

            // Start a new line having the current valley and connect it with others in the left.
            valley->used = true;

            Line *new_line = new Line(valley->valley_id);
            new_line = connect_valleys(i - 1, valley, new_line, valleys_min_abs_dist);
            new_line->generate_initial_points(chunk_width, color_img.cols, map_valley);

            if (new_line->valleys_ids.size() > 1) {
                this->initial_lines.push_back(new_line);
            } else {
                single_valley.push_back(new_line);
            }
        }
    }

    // Sort the lines
    sort(this->initial_lines.begin(), this->initial_lines.end(), Line::comp_min_row_position);

    // Get the weight
    float weight;
    if (total_avg_height * 1.2 < avg_space_height)
        weight = 0.85;
    else weight = 0.6;

    vector<Line *> new_lines;
    new_lines.push_back(this->initial_lines[this->initial_lines.size() - 1]);
    cout << total_avg_height << " " << avg_space_height << " " << this->CHUNCKS_TO_BE_PROCESSED << " " << weight
         << endl;
    // Check for lines near each other and remove them
    for (int i = this->initial_lines.size() - 2; i >= 0; --i) {
        if (-this->initial_lines[i]->points[0].x + new_lines.back()->points[0].x >=
            weight * (avg_line_height + avg_space_height))
            new_lines.push_back(this->initial_lines[i]);
    }
    reverse(new_lines.begin(), new_lines.end());
    this->initial_lines = new_lines;
}

void
LineSegmentation::show_lines(string path) {
    Mat img_clone = this->color_img.clone();

    for (auto line : initial_lines) {
        int last_row = -1;

        for (auto point : line->points) {
            img_clone.at<Vec3b>(point.x, point.y) = TEST_LINE_COLOR;

            // Check and draw vertical lines if found.
            if (last_row != -1 && point.x != last_row) {
                for (int i = min(last_row, point.x); i < max(last_row, point.x); i++) {
                    img_clone.at<Vec3b>(i, point.y) = TEST_LINE_COLOR;
                }
            }

            last_row = point.x;
        }
    }
    imwrite(path, img_clone);
}


void
LineSegmentation::generate_regions() {
    this->line_regions = vector<Region *>();

    // Add first region
    Region *r = new Region(nullptr, this->initial_lines[0]);
    r->update_region(this->binary_img, 0);
    this->initial_lines[0]->above = r;

    // Add rest of regions
    for (int i = 0; i < this->initial_lines.size(); ++i) {
        Line *top_line = this->initial_lines[i];
        Line *bottom_line = (i == this->initial_lines.size() - 1) ? nullptr : this->initial_lines[i + 1];

        // Assign lines to region
        Region *r = new Region(top_line, bottom_line);
        r->update_region(this->binary_img, i);

        // Assign regions to lines
        if (top_line != nullptr)
            top_line->below = r;

        if (bottom_line != nullptr)
            bottom_line->above = r;

        this->line_regions.push_back(r);
    }
}


void
LineSegmentation::repair_lines() {
    // Loop over the regions.
    for (Line *line : initial_lines) {
        for (int i = 0; i < line->points.size(); i++) {
            Point &point = line->points[i];

            int x = (line->points[i]).x, y = (line->points[i]).y;

            // Check for vertical line intersection
            if (this->binary_img.at<uchar>(point.x, point.y) == 255) {
                if (i == 0) continue;
                bool black_found = false;

                if (line->points[i - 1].x != line->points[i].x) {
                    int min_row = min(line->points[i - 1].x, line->points[i].x);
                    int max_row = max(line->points[i - 1].x, line->points[i].x);
                    for (int j = min_row; j <= max_row && !black_found; ++j) {
                        if (this->binary_img.at<uchar>(j, line->points[i - 1].y) == 0) {
                            x = j, y = line->points[i - 1].y;
                            black_found = true;
                        }
                    }
                }
                if (!black_found) continue;
            }

            for (auto contour : this->contours) {
                // Check line & contour intersection
                if (y >= contour.tl().x && y <= contour.br().x && x >= contour.tl().y && x <= contour.br().y) {

                    // If contour is longer than the average height ignore.
                    // if (contour.br().y - contour.tl().y > this->avg_line_height * 1.5) continue;

                    bool is_component_above = component_belongs_to_above_region(*line, contour);

                    int new_row;
                    if (!is_component_above) {
                        new_row = contour.tl().y;
                        line->min_row_position = min(line->min_row_position, new_row);
                    } else {
                        new_row = contour.br().y;
                        line->max_row_position = max(new_row, line->max_row_position);
                    }

                    for (int k = contour.tl().x; k < contour.tl().x + contour.width; k++) {
                        line->points[k].x = new_row;
                    }
                    i = (contour.br().x);

                    break; // Contour found
                }
            }
        }
    }
}

bool LineSegmentation::component_belongs_to_above_region(Line &line, Rect &contour) {
    // Calculate probabilities.
    vector<int> probAbovePrimes(Utilities::primes.size(), 0);
    vector<int> probBelowPrimes(Utilities::primes.size(), 0);
    int n = 0;

    for (int i_contour = contour.tl().x; i_contour < contour.tl().x + contour.width; i_contour++) {
        for (int j_contour = contour.tl().y; j_contour < contour.tl().y + contour.height; j_contour++) {
            if (binary_img.at<uchar>(j_contour, i_contour) == 255) continue;

            n++;

            Mat contour_point = Mat::zeros(1, 2, CV_32F);
            contour_point.at<float>(0, 0) = j_contour;
            contour_point.at<float>(0, 1) = i_contour;

            int newProbAbove = (int) ((line.above != nullptr) ? (line.above->bi_variate_gaussian_density(
                    contour_point.clone())) : 0);
            int newProbBelow = (int) ((line.below != nullptr) ? (line.below->bi_variate_gaussian_density(
                    contour_point.clone())) : 0);

            Utilities::addPrimesToVector(newProbAbove, probAbovePrimes);
            Utilities::addPrimesToVector(newProbBelow, probBelowPrimes);
        }
    }

    int prob_above = 0, prob_below = 0;

    for (int k = 0; k < probAbovePrimes.size(); ++k) {
        int mini = min(probAbovePrimes[k], probBelowPrimes[k]);

        probAbovePrimes[k] -= mini;
        probBelowPrimes[k] -= mini;

        prob_above += probAbovePrimes[k] * Utilities::primes[k];
        prob_below += probBelowPrimes[k] * Utilities::primes[k];
    }

    return prob_above < prob_below;
}

vector<Mat>
LineSegmentation::get_regions() {
    vector<Mat> ret;
    for (auto region : this->line_regions) {
        ret.push_back(region->region.clone());
    }

    return ret;
}