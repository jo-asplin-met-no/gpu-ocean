#include "netcdfwriter.h"
#include <sstream>
#include <iomanip>
#include <vector>
#include <ctime>
#include <cstring>

using std::endl;
using std::cout;
using std::setw;
using std::setprecision;
using std::setfill;

struct NetCDFWriter::NetCDFWriterImpl
{
    boost::shared_ptr<NcFile> file;

    struct {
        struct {
            NcDim* x;       //!< integer x coordinates
            NcDim* y;       //!<
            NcDim* x_half;  //!< x coordinates at center of cells
            NcDim* y_half;  //!<
            NcDim* t;       //!< Time
        } dims;

        struct {
            NcVar* H;
            NcVar* eta;
            NcVar* U;
            NcVar* V;
            NcVar* x;        //!< x
            NcVar* y;        //!< y
            NcVar* x_half;
            NcVar* y_half;
            NcVar* t;        //!< time
        } vars;
    } layout;

    long timestepCounter; // internal timestep counter
    unsigned int nx;
    unsigned int ny;

    NetCDFWriterImpl();
};

NetCDFWriter::NetCDFWriterImpl::NetCDFWriterImpl()
    : timestepCounter(0)
{
}

NetCDFWriter::NetCDFWriter()
    : pimpl(new NetCDFWriterImpl())
{
    std::stringstream ss;
    time_t secs = time(0);
    tm *t = localtime(&secs);
    ss << "fbl_"
       << setw(2) << setfill('0') << setprecision(2) << t->tm_year + 1900 << "_"
       << setw(2) << setfill('0') << setprecision(2) << t->tm_mon + 1 << "_"
       << setw(2) << setfill('0') << setprecision(2) << t->tm_mday << "_"
       << setw(2) << setfill('0') << setprecision(2) << t->tm_hour << "_"
       << setw(2) << setfill('0') << setprecision(2) << t->tm_min << "_"
       << setw(2) << setfill('0') << setprecision(2) << t->tm_sec << ".nc";
    initFile(ss.str());
}

NetCDFWriter::NetCDFWriter(std::string fname)
    :  pimpl(new NetCDFWriterImpl())
{
    initFile(fname);
}

void NetCDFWriter::initFile(std::string fname)
{
    pimpl->file.reset(new NcFile(fname.c_str(), NcFile::New));
    if (!pimpl->file->is_valid()) {
        std::stringstream ss;
        ss << "Could not create '" << fname << "'." << endl;
        ss << "Check that it does not exist, or that your disk is full." << endl;
        throw(ss.str());
    }
    memset(&pimpl->layout, 0, sizeof(pimpl->layout));
}

NetCDFWriter::~NetCDFWriter()
{
    pimpl->file->sync();
    if (!pimpl->file->close()) {
        throw("Error: Couldn't close NetCDF file!");
    }
    pimpl->file.reset();
}

void NetCDFWriter::init(int nx, int ny, float dt, float dx, float dy, float f, float r, float *H)
{
    pimpl->nx = nx;
    pimpl->ny = ny;

    // create dimensions
    pimpl->layout.dims.x = pimpl->file->add_dim("X", nx+1);
    pimpl->layout.dims.y = pimpl->file->add_dim("Y", ny+1);
    pimpl->layout.dims.x_half = pimpl->file->add_dim("X_half", nx);
    pimpl->layout.dims.y_half = pimpl->file->add_dim("Y_half", ny);
    pimpl->layout.dims.t = pimpl->file->add_dim("T");

    // create indexing variables
    pimpl->layout.vars.x = pimpl->file->add_var("X", ncFloat, pimpl->layout.dims.x);
    pimpl->layout.vars.y = pimpl->file->add_var("Y", ncFloat, pimpl->layout.dims.y);
    pimpl->layout.vars.x_half = pimpl->file->add_var("X_half", ncFloat, pimpl->layout.dims.x_half);
    pimpl->layout.vars.y_half = pimpl->file->add_var("Y_half", ncFloat, pimpl->layout.dims.y_half);
    pimpl->layout.vars.x->add_att("description", "Longitudal coordinate for values given at grid cell intersections");
    pimpl->layout.vars.y->add_att("description", "Latitudal coordinate for values given at grid cell intersections");
    pimpl->layout.vars.x_half->add_att("description", "Longitudal coordinate for values given at grid cell centers");
    pimpl->layout.vars.y_half->add_att("description", "Latitudal coordinate for values given at grid cell centers");

    pimpl->layout.vars.t = pimpl->file->add_var("T", ncFloat, pimpl->layout.dims.t);
    pimpl->layout.vars.t->add_att("description", "Time");

    // write parameters
    pimpl->file->add_att("nx", static_cast<int>(nx));
    pimpl->file->add_att("ny", static_cast<int>(ny));
    pimpl->file->add_att("dt", dt);
    pimpl->file->add_att("dx", dx);
    pimpl->file->add_att("dy", dy);
    pimpl->file->add_att("f", f);
    pimpl->file->add_att("r", r);

    // write contents of spatial variables
    std::vector<float> tmp;
    tmp.resize(nx+1);
    for (unsigned int i=0; i<tmp.size(); ++i)
        tmp[i] = i * dx;
    pimpl->layout.vars.x->put(&tmp[0], tmp.size());

    tmp.resize(ny+1);
    for (unsigned int i=0; i<tmp.size(); ++i)
        tmp[i] = i * dy;
    pimpl->layout.vars.y->put(&tmp[0], tmp.size());

    tmp.resize(nx);
    for (unsigned int i=0; i<tmp.size(); ++i)
        tmp[i] = (i+0.5f) * dx;
    pimpl->layout.vars.x_half->put(&tmp[0], tmp.size());

    tmp.resize(ny);
    for (unsigned int i=0; i<tmp.size(); ++i)
        tmp[i] = (i+0.5f) * dy;
    pimpl->layout.vars.y_half->put(&tmp[0], tmp.size());

    pimpl->file->sync();

    // create initial condition variables
    pimpl->layout.vars.H = pimpl->file->add_var("H", ncFloat, pimpl->layout.dims.y_half, pimpl->layout.dims.x_half);
    pimpl->layout.vars.H->add_att("description", "Mean water depth");

    // create the timestep variables
    pimpl->layout.vars.eta = pimpl->file->add_var("eta", ncFloat, pimpl->layout.dims.t, pimpl->layout.dims.y_half, pimpl->layout.dims.x_half);
    pimpl->layout.vars.U = pimpl->file->add_var("U", ncFloat, pimpl->layout.dims.t, pimpl->layout.dims.y_half, pimpl->layout.dims.x);
    pimpl->layout.vars.V = pimpl->file->add_var("V", ncFloat, pimpl->layout.dims.t, pimpl->layout.dims.y, pimpl->layout.dims.x_half);

    pimpl->layout.vars.eta->add_att("description", "Water elevation disturbances");
    pimpl->layout.vars.U->add_att("description", "Longitudal water discharge");
    pimpl->layout.vars.V->add_att("description", "Latitudal water discharge");

    // set compression
    nc_def_var_deflate(pimpl->file->id(), pimpl->layout.vars.H->id(), 1, 1, 2);
    nc_def_var_deflate(pimpl->file->id(), pimpl->layout.vars.eta->id(), 1, 1, 2);
    nc_def_var_deflate(pimpl->file->id(), pimpl->layout.vars.U->id(), 1, 1, 2);
    nc_def_var_deflate(pimpl->file->id(), pimpl->layout.vars.V->id(), 1, 1, 2);

    // write data
    pimpl->layout.vars.H->put(H, ny, nx);

    pimpl->file->sync();
}

void NetCDFWriter::writeTimestep(float *eta, float *U, float *V, float t)
{
    pimpl->layout.vars.t->set_cur(pimpl->timestepCounter);
    pimpl->layout.vars.t->put(&t, 1);

    pimpl->layout.vars.eta->set_cur(pimpl->timestepCounter, 0, 0);
    pimpl->layout.vars.eta->put(eta, 1, pimpl->ny, pimpl->nx);

    pimpl->layout.vars.U->set_cur(pimpl->timestepCounter, 0, 0);
    pimpl->layout.vars.U->put(U, 1, pimpl->ny, pimpl->nx + 1);

    pimpl->layout.vars.V->set_cur(pimpl->timestepCounter, 0, 0);
    pimpl->layout.vars.V->put(V, 1, pimpl->ny + 1, pimpl->nx);

    pimpl->file->sync();
    ++pimpl->timestepCounter;
}