/*
  Copyright 2010 SINTEF ICT, Applied Mathematics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_CORNERPOINTCHOPPER_HEADER_INCLUDED
#define OPM_CORNERPOINTCHOPPER_HEADER_INCLUDED

#include <dune/common/EclipseGridParser.hpp>
#include <dune/common/param/ParameterGroup.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

namespace Dune
{

    class CornerPointChopper
    {
    public:
        CornerPointChopper(const std::string& file)
            : parser_(file)
        {
            for (int dd = 0; dd < 3; ++dd) {
                dims_[dd] = parser_.getSPECGRID().dimensions[dd];
            }

            int layersz = 8*dims_[0]*dims_[1];
            const std::vector<double>& ZCORN = parser_.getFloatingPointValue("ZCORN");
            botmax_ = *std::max_element(ZCORN.begin(), ZCORN.begin() + layersz/2);
            topmin_ = *std::min_element(ZCORN.begin() + dims_[2]*layersz - layersz/2,
                                        ZCORN.begin() + dims_[2]*layersz);
            std::cout << "Parsed grdecl file with dimensions ("
                      << dims_[0] << ", " << dims_[1] << ", " << dims_[2] << ")" << std::endl;
        }




        const int* dimensions() const
        {
            return dims_;
        }




        const int* newDimensions() const
        {
            return new_dims_;
        }




        const std::pair<double, double> zLimits() const
        {
            return std::make_pair(botmax_, topmin_);
        }




        void chop(int imin, int imax, int jmin, int jmax, double zmin, double zmax)
        {
            new_dims_[0] = imax - imin;
            new_dims_[1] = jmax - jmin;

            // Filter the coord field
            const std::vector<double>& COORD = parser_.getFloatingPointValue("COORD");
            int num_coord = COORD.size();
            if (num_coord != 6*(dims_[0] + 1)*(dims_[1] + 1)) {
                std::cerr << "Error! COORD size (" << COORD.size() << ") not consistent with SPECGRID\n";
                throw std::runtime_error("Inconsistent COORD and SPECGRID.");
            }
            int num_new_coord = 6*(new_dims_[0] + 1)*(new_dims_[1] + 1);
            new_COORD_.resize(num_new_coord, 1e100);
            for (int j = jmin; j < jmax + 1; ++j) {
                for (int i = imin; i < imax + 1; ++i) {
                    int pos = (dims_[0] + 1)*j + i;
                    int new_pos = (new_dims_[0] + 1)*(j-jmin) + (i-imin);
                    std::copy(COORD.begin() + 6*pos, COORD.begin() + 6*(pos + 1), new_COORD_.begin() + 6*new_pos);
                }
            }

            // Get the z limits, check if they must be changed to make a shoe-box.
            // This means that zmin must be greater than or equal to the highest
            // coordinate of the bottom surface, while zmax must be less than or
            // equal to the lowest coordinate of the top surface.
            int layersz = 8*dims_[0]*dims_[1];
            const std::vector<double>& ZCORN = parser_.getFloatingPointValue("ZCORN");
            int num_zcorn = ZCORN.size();
            if (num_zcorn != layersz*dims_[2]) {
                std::cerr << "Error! ZCORN size (" << ZCORN.size() << ") not consistent with SPECGRID\n";
                throw std::runtime_error("Inconsistent ZCORN and SPECGRID.");
            }

            zmin = std::max(zmin, botmax_);
            zmax = std::min(zmax, topmin_);
            if (zmin >= zmax) {
                std::cerr << "Error: zmin >= zmax (zmin = " << zmin << ", zmax = " << zmax << ")\n";
                throw std::runtime_error("zmin >= zmax");
            }
            std::cout << "zmin = " << zmin << ", zmax = " << zmax << std::endl;

            // We must find the maximum and minimum k value for the given z limits.
            // First, find the first layer with a z-coordinate strictly above zmin.
            int kmin = -1;
            for (int k = 0; k < dims_[2]; ++k) {
                double layer_max = *std::max_element(ZCORN.begin() + k*layersz, ZCORN.begin() + (k + 1)*layersz);
                if (layer_max > zmin) {
                    kmin = k;
                    break;
                }
            }
            // Then, find the last layer with a z-coordinate strictly below zmax.
            int kmax = -1;
            for (int k = dims_[2]; k > 0; --k) {
                double layer_min = *std::min_element(ZCORN.begin() + (k - 1)*layersz, ZCORN.begin() + k*layersz);
                if (layer_min < zmax) {
                    kmax = k;
                    break;
                }
            }
            new_dims_[2] = kmax - kmin;

            // Filter the ZCORN field, build mapping from new to old cells.
            new_ZCORN_.resize(8*new_dims_[0]*new_dims_[1]*new_dims_[2], 1e100);
            new_to_old_cell_.resize(new_dims_[0]*new_dims_[1]*new_dims_[2], -1);
            int cellcount = 0;
            int delta[3] = { 1, 2*dims_[0], 4*dims_[0]*dims_[1] };
            int new_delta[3] = { 1, 2*new_dims_[0], 4*new_dims_[0]*new_dims_[1] };
            for (int k = kmin; k < kmax; ++k) {
                for (int j = jmin; j < jmax; ++j) {
                    for (int i = imin; i < imax; ++i) {
                        new_to_old_cell_[cellcount++] = dims_[0]*dims_[1]*k + dims_[0]*j + i;
                        int old_ix = 2*(i*delta[0] + j*delta[1] + k*delta[2]);
                        int new_ix = 2*((i-imin)*new_delta[0] + (j-jmin)*new_delta[1] + (k-kmin)*new_delta[2]);
                        int old_indices[8] = { old_ix, old_ix + delta[0],
                                               old_ix + delta[1], old_ix + delta[1] + delta[0],
                                               old_ix + delta[2], old_ix + delta[2] + delta[0],
                                               old_ix + delta[2] + delta[1], old_ix + delta[2] + delta[1] + delta[0] };
                        int new_indices[8] = { new_ix, new_ix + new_delta[0],
                                               new_ix + new_delta[1], new_ix + new_delta[1] + new_delta[0],
                                               new_ix + new_delta[2], new_ix + new_delta[2] + new_delta[0],
                                               new_ix + new_delta[2] + new_delta[1], new_ix + new_delta[2] + new_delta[1] + new_delta[0] };
                        for (int cc = 0; cc < 8; ++cc) {
                            new_ZCORN_[new_indices[cc]] = std::min(zmax, std::max(zmin, ZCORN[old_indices[cc]]));
                        }
                    }
                }
            }
            
            filterIntegerField("ACTNUM", new_ACTNUM_);
            filterDoubleField("PORO", new_PORO_);
            filterDoubleField("PERMX", new_PERMX_);
            filterDoubleField("PERMY", new_PERMY_);
            filterDoubleField("PERMZ", new_PERMZ_);
            filterIntegerField("SATNUM", new_SATNUM_);
        }




        EclipseGridParser subparser()
        {
            EclipseGridParser sp(parser_);
            boost::shared_ptr<SPECGRID> sg(new SPECGRID);
            for (int dd = 0; dd < 3; ++dd) {
                sg->dimensions[dd] = new_dims_[dd];
            }
            sp.setSpecialField("SPECGRID", sg);
            sp.setFloatingPointField("COORD", new_COORD_);
            sp.setFloatingPointField("ZCORN", new_ZCORN_);
            sp.setIntegerField("ACTNUM", new_ACTNUM_);
            sp.setFloatingPointField("PORO", new_PORO_);
            sp.setFloatingPointField("PERMX", new_PERMX_);
            sp.setFloatingPointField("PERMY", new_PERMY_);
            sp.setFloatingPointField("PERMZ", new_PERMZ_);
            sp.setIntegerField("SATNUM", new_SATNUM_);
            return sp;
        }




        void writeGrdecl(const std::string& filename)
        {
            // Output new versions of SPECGRID, COORD, ZCORN, ACTNUM, PERMX, PORO, SATNUM.
            std::ofstream out(filename.c_str());
            if (!out) {
                std::cerr << "Could not open file " << filename << "\n";
                throw std::runtime_error("Could not open output file.");
            }
            out << "SPECGRID\n" << new_dims_[0] << ' ' << new_dims_[1] << ' ' << new_dims_[2]
                << " 1 F\n/\n\n";
            out << "COORD\n";
            int num_new_coord = new_COORD_.size();
            for (int i = 0; i < num_new_coord/6; ++i) {
                for (int j = 0; j < 6; ++j) {
                    out << "  " << new_COORD_[6*i + j];
                }
                out << '\n';
            }
            out << "/\n\n";
            out << "ZCORN\n";
            int num_new_zcorn = new_ZCORN_.size();
            assert(num_new_zcorn%8 == 0);
            for (int i = 0; i < num_new_zcorn/8; ++i) {
                for (int j = 0; j < 8; ++j) {
                    out << "  " << new_ZCORN_[8*i + j];
                }
                out << '\n';
            }
            out << "/\n\n";

            outputField(out, new_ACTNUM_, "ACTNUM");
            outputField(out, new_PORO_, "PORO");
            outputField(out, new_PERMX_, "PERMX");
            outputField(out, new_PERMY_, "PERMY");
            outputField(out, new_PERMZ_, "PERMZ");
            outputField(out, new_SATNUM_, "SATNUM");
        }

    private:
        EclipseGridParser parser_;
        double botmax_;
        double topmin_;
        std::vector<double> new_COORD_;
        std::vector<double> new_ZCORN_;
        std::vector<int> new_ACTNUM_;
        std::vector<double> new_PORO_;
        std::vector<double> new_PERMX_;
        std::vector<double> new_PERMY_;
        std::vector<double> new_PERMZ_;
        std::vector<int> new_SATNUM_;
        int dims_[3];
        int new_dims_[3];
        std::vector<int> new_to_old_cell_;


        template <typename T>
        void outputField(std::ostream& os,
                         const std::vector<T>& field,
                         const std::string& keyword)
        {
            if (field.empty()) return;
            os << keyword << '\n';
            int sz = field.size();
            T last = std::numeric_limits<T>::max();
            int repeats = 0;
            for (int i = 0; i < sz; ++i) {
                T val = field[i];
                if (val == last) {
                    ++repeats;
                } else {
                    if (repeats == 1) {
                        os << last << '\n';
                    } else if (repeats > 1) {
                        os << repeats << '*' << last << '\n';
                    }
                    last = val;
                    repeats = 1;
                }
            }
            if (repeats == 1) {
                os << last << '\n';
            } else if (repeats > 1) {
                os << repeats << '*' << last << '\n';
            }
            os << "/\n\n";
        }


        template <typename T>
        void filterField(const std::vector<T>& field,
                                std::vector<T>& output_field)
        {
            int sz = new_to_old_cell_.size();
            output_field.resize(sz);
            for (int i = 0; i < sz; ++i) {
                output_field[i] = field[new_to_old_cell_[i]];
            }
        }

        void filterDoubleField(const std::string& keyword, std::vector<double>& output_field)
        {
            if (parser_.hasField(keyword)) {
                const std::vector<double>& field = parser_.getFloatingPointValue(keyword);
                filterField(field, output_field);
            }
        }

        void filterIntegerField(const std::string& keyword, std::vector<int>& output_field)
        {
            if (parser_.hasField(keyword)) {
                const std::vector<int>& field = parser_.getIntegerValue(keyword);
                filterField(field, output_field);
            }
        }

    };

}




#endif // OPM_CORNERPOINTCHOPPER_HEADER_INCLUDED
