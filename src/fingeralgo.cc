#include <fstream>
#include <cmath>
#include <iostream>

#include "../include/fingeralgo.h"

const char FingerAlgo::FINGERPRINT[100] = "./data/fingerprint/fingerprint";
const double FingerAlgo::RADIUS = 15;

void print_vector(const std::vector<double>& v) {
        for (auto val : v)
                std::cout << val << " ";
        std::cout << std::endl;
}


FingerAlgo::~FingerAlgo() {}

void FingerAlgo::prepare() {
	Algorithm::prepare();
	load_fingerprint();
}

Location FingerAlgo::do_work(const std::vector<RssiInfo>& rssi_infos) {
        for (size_t i = 0; i < rssi_infos.size(); ++i) {
                double rssi = rssi_infos[i].rssi;
                this->rssis.push_back(rssi);
                double rdis = 1 / rssi;
                if (rssi < Algorithm::EPS) rdis = 0;
                this->rough_dists.push_back(rdis);
        }
        
	int t = find_nearest();
	pick_candidates(t);

	int last_t;
	do {
		last_t = t;
		adjust_distance(last_t);
		t = least_error();
	}while (last_t != t);

	sort_candidates(this->errors);
	generate_coeffs(this->errors);
	esti_location();
	return Location{time_stamp(), this->xe, this->ye, this->ze};
}


std::vector<Location> FingerAlgo::debug(const std::vector<RssiInfo>& rssi_infos) {
	std::vector<Location> ret;

//        std::cout << "rssis ";
        for (size_t i = 0; i < rssi_infos.size(); ++i) {
                double rssi = rssi_infos[i].rssi;
                this->rssis.push_back(rssi);
 //               std::cout << rssi << " ";
        }
  //      std::cout << std::endl;

        this->rough_dists = rough_distance(this->rssis);

	long long timestamp = Algorithm::time_stamp();

	int t = find_nearest();
//        std::cout << "neart t " << t << " rssis : ";
//        print_vector(this->samples[t].rssis);


	pick_candidates(t);
	const Sample& sam = this->samples[t];
	ret.push_back(Location{timestamp, sam.x, sam.y, sam.z});
//        std::cout << "nearest (" << sam.x << ", " << sam.y << ", " << sam.z << ")" << std::endl;

	std::vector<double> dist_errors;
	for (auto index : candidates) {
		const Sample& sam = this->samples[index];
		double dist = Algorithm::distance(this->rssis, sam.rssis);
		dist_errors.push_back(dist);
	}
	
	sort_candidates(dist_errors);
	generate_coeffs(dist_errors);
	esti_location();

//        std::cout << "esti1(" << this->xe << ", " << this->ye << ", " << this->ze << ")" << std::endl;
	ret.push_back(Location{timestamp, this->xe, this->ye, this->ze});

	int last_t;
        int cnt = 0;
	do {
		last_t = t;
		adjust_distance(last_t);
		t = least_error();
                cnt++;
	}while (last_t != t && cnt < 200);

	sort_candidates(this->errors);
	generate_coeffs(this->errors);
	esti_location();

 //       std::cout << "esti2(" << this->xe << ", " << this->ye << ", " << this->ze << ")" << std::endl;
	ret.push_back(Location{timestamp, this->xe, this->ye, this->ze});
	return ret;
}

void FingerAlgo::load_fingerprint() {
	std::ifstream fin(FINGERPRINT);
	this->samples.clear();
        int x, y, z;
        while(fin >> x >> y >> z) {
                Sample sam;
                sam.x = x; sam.y = y; sam.z = z;
                int uid;
                double rssi;
                for (int i = 0; i < Receiver::MAX_UID; ++i) {
                        fin >> uid >> rssi;
			sam.rssis.push_back(rssi);
			const LedInfo& led_info = get_ledinfos()[uid - 1];
                        double sam_dist = distance(x, y, z, led_info.x, led_info.y, led_info.z);
			sam.dists.push_back(sam_dist);
                        if (rssi < Algorithm::EPS)
                                sam.adjust.push_back(0);
                        else
                                sam.adjust.push_back(sam_dist / sqrt(led_info.P0 / rssi));
                }
                this->samples.push_back(sam);
        }
}

std::vector<double> FingerAlgo::rough_distance(const std::vector<double>& rssis) const {
        std::vector<double> ret;
        for (size_t i = 0; i < this->rssis.size(); ++i) {
                double rssi = this->rssis[i];
                if (rssi < Algorithm::EPS)
                        ret.push_back(0);
                else
                        ret.push_back(sqrt(get_ledinfos()[i].P0 / rssi));
        }
        return ret;
}

int FingerAlgo::find_nearest() const {
        int ret = 0;
        double min = Algorithm::distance(this->samples[0].rssis, this->rssis);
        for (size_t i = 1; i < this->samples.size(); ++i) {
                double dis = Algorithm::distance(this->samples[i].rssis, this->rssis);
                if (dis < min) {
                        min = dis;
                        ret = i;
                }
        }
        return ret;
}

void FingerAlgo::pick_candidates(int nearest) {
	this->candidates.clear();
	const Sample& near_sam = this->samples[nearest];
	for (size_t i = 0; i < this->samples.size(); ++i) {
		const Sample& sam = this->samples[i];
		double dist = distance(near_sam.x, near_sam.y, near_sam.z, sam.x, sam.y, sam.z);
		if (dist < RADIUS) {
			this->candidates.push_back(i);
                }
	}
}

void FingerAlgo::adjust_distance(int nearest) {
	this->esti_dists.clear();
	const std::vector<double>& adjust = this->samples[nearest].adjust;
	for (size_t i = 0; i < this->rough_dists.size(); ++i) {
		this->esti_dists.push_back(this->rough_dists[i] * adjust[i]);
                //std::cout << "dist " << rough_dists[i] << " is adjusted to " << this->rough_dists[i] * adjust[i] <<
                //" by adjust " << adjust[i] << std::endl;
	}
}

void FingerAlgo::sort_candidates(std::vector<double>& errors) {
	for (size_t i = 1; i < errors.size(); ++i) {
		if (errors[i] < errors[i - 1]) {
			int j = i - 1;
			double tmp = errors[i];
			int index = this->candidates[i];
			errors[i] = errors[i - 1];
			this->candidates[i] = this->candidates[i - 1];
			while (tmp < errors[j]) {
				errors[j + 1] = errors[j];
				this->candidates[j + 1] = this->candidates[j];
				j--;
			}
			errors[j + 1] = tmp;
			this->candidates[j + 1] = index;
		}
	}
}

int FingerAlgo::least_error() {
	this->errors.clear();
	for (auto index : candidates) {
		double error = Algorithm::distance(this->esti_dists, this->samples[index].dists);
                //std::cout << "esti_dist ";
                //print_vector(this->esti_dists);
                //std::cout << "samples " << index << " : ";
                //print_vector(this->samples[index].dists);
                //std::cout << "error is " << error << std::endl;
		this->errors.push_back(error);
	}

	double min = this->errors[0];
	int ret = this->candidates[0];
	for (size_t i = 1; i < this->errors.size(); ++i) {
		if (this->errors[i] < min) {
			min = this->errors[i];
			ret = this->candidates[i];
		}
	}
	return ret;
}

void FingerAlgo::generate_coeffs(const std::vector<double>& errors) {
	double sum = 0;
	std::vector<double> temps;
	for (size_t i = 0; i < FingerAlgo::K; ++i) {
//                std::cout << "error " << errors[i] << std::endl;
                double error = errors[i];
		double temp = 1 / (error * error);
                if (error < Algorithm::EPS) temp = Algorithm::EPS;
		sum += temp;
		temps.push_back(temp);
	}

	this->coeffs.clear();
	for (size_t i = 0; i < FingerAlgo::K; ++i) {
		this->coeffs.push_back(temps[i] / sum);
//                std::cout << "coeff " << coeffs[i] << std::endl;
	}
}

void FingerAlgo::esti_location() {
	this->xe = 0; this->ye = 0; this->ze = 0;
        for (size_t i = 0; i < FingerAlgo::K; ++i) {
		this->xe += samples[candidates[i]].x * coeffs[i];
		this->ye += samples[candidates[i]].y * coeffs[i];
		this->ze += samples[candidates[i]].z * coeffs[i];
	}
}
