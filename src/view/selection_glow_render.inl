// Pixel-width boundary quads guarantee a halo even where core-profile GL
// implementations clamp glLineWidth to one pixel. Geometry stays unchanged.
static void drawSelectedFaceGlow(){
    std::vector<float> boundary=g_selectionFaceBoundary;
    // The body uses the same pixel halo as exact faces. Geometrically welded
    // front/back triangle edges also find silhouettes of smooth closed bodies.
    // This is display evidence; the named face span remains the CAD identity.
    if(selBody>=0&&(size_t)selBody<g_instancePart.size()&&displayInstance((size_t)selBody)&&g_pickKind==0){
        size_t part=g_instancePart[(size_t)selBody];if(part<g_cpuMeshes.size()){
            const auto& mesh=g_cpuMeshes[part];const auto& world=g_instanceWorld[(size_t)selBody];
            using Point=std::array<float,3>;using Edge=std::pair<Point,Point>;struct Sides{bool front=false,back=false;unsigned count=0;};std::map<Edge,Sides> edges;
            for(size_t at=0;at+2<mesh.idx.size();at+=3){Point points[3];Vec3 vertices[3];bool valid=true;
                for(int k=0;k<3;++k){size_t index=(size_t)mesh.idx[at+k]*3;if(index+2>=mesh.pos.size()){valid=false;break;}points[k]={mesh.pos[index],mesh.pos[index+1],mesh.pos[index+2]};vertices[k]=world.transformPoint({points[k][0],points[k][1],points[k][2]});}if(!valid)continue;
                Vec3 normal=(vertices[1]-vertices[0]).cross(vertices[2]-vertices[0]);Vec3 toward=g_cameraOrtho?cameraFrame(yaw,pitch,cameraRoll).outward:camEye()-(vertices[0]+vertices[1]+vertices[2])/3.;bool front=normal.dot(toward)>=0;
                for(int k=0;k<3;++k){auto a=points[k],b=points[(k+1)%3];if(b<a)std::swap(a,b);auto& side=edges[{a,b}];side.front|=front;side.back|=!front;++side.count;}
            }
            for(const auto& edge:edges)if(edge.second.count==1||(edge.second.front&&edge.second.back))for(const auto& p:{edge.first.first,edge.first.second}){auto point=world.transformPoint({p[0],p[1],p[2]});boundary.insert(boundary.end(),{(float)point.x,(float)point.y,(float)point.z});}
        }
    }
    if(boundary.empty())return;static GLuint vao=0,vbo=0;
    if(!vao){glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);}
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);
    auto identity=Mat4::identity();float matrix[16];for(int i=0;i<16;++i)matrix[i]=(float)identity.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,matrix);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
    const auto camera=cameraProjection()*cameraView();auto project=[&](Vec3 point,float& x,float& y,float& z){double w=camera.m[3]*point.x+camera.m[7]*point.y+camera.m[11]*point.z+camera.m[15];if(w<=1e-6)return false;auto clip=camera.transformPoint(point);x=viewX+(float)((clip.x/w+1)*viewW*.5);y=viewY+(float)((1-clip.y/w)*viewH*.5);z=(float)(clip.z/w)-.000002f;return true;};
    const float widths[]={17,11,6,2.4f},alpha[]={.055f,.10f,.22f,.97f};
    for(int pass=0;pass<4;++pass){std::vector<float> triangles;
        auto put=[&](float x,float y,float z){triangles.insert(triangles.end(),{2*(x-viewX)/viewW-1,1-2*(y-viewY)/viewH,z});};
        for(size_t at=0;at+5<boundary.size();at+=6){float ax,ay,az,bx,by,bz;if(!project({boundary[at],boundary[at+1],boundary[at+2]},ax,ay,az)||!project({boundary[at+3],boundary[at+4],boundary[at+5]},bx,by,bz))continue;float dx=bx-ax,dy=by-ay,length=std::hypot(dx,dy);if(length<.01f)continue;float nx=-dy/length*widths[pass]*.5f,ny=dx/length*widths[pass]*.5f;put(ax+nx,ay+ny,az);put(ax-nx,ay-ny,az);put(bx+nx,by+ny,bz);put(bx+nx,by+ny,bz);put(ax-nx,ay-ny,az);put(bx-nx,by-ny,bz);}
        glUniform4f(glGetUniformLocation(pSketch,"tint"),1,.69f,.25f,alpha[pass]);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(triangles.size()*sizeof(float)),triangles.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,(GLsizei)triangles.size()/3);
    }glDepthMask(GL_TRUE);glViewport(0,0,winW,winH);
}
