/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2003 Robert Osfield 
 *
 * This library is open source and may be redistributed and/or modified under  
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or 
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/
#include <osg/GLExtensions>
#include <osg/Texture2D>
#include <osg/State>
#include <osg/GLU>

typedef void (APIENTRY * MyCompressedTexImage2DArbProc) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);

using namespace osg;

Texture2D::Texture2D():
            _textureWidth(0),
            _textureHeight(0),
            _numMimpmapLevels(0)
{
    setUseHardwareMipMapGeneration(true);
}

Texture2D::Texture2D(const Texture2D& text,const CopyOp& copyop):
            Texture(text,copyop),
            _image(copyop(text._image.get())),
            _textureWidth(text._textureWidth),
            _textureHeight(text._textureHeight),
            _numMimpmapLevels(text._numMimpmapLevels),
            _subloadCallback(text._subloadCallback)
{
}

Texture2D::~Texture2D()
{
}

int Texture2D::compare(const StateAttribute& sa) const
{
    // check the types are equal and then create the rhs variable
    // used by the COMPARE_StateAttribute_Paramter macro's below.
    COMPARE_StateAttribute_Types(Texture2D,sa)

    if (_image!=rhs._image) // smart pointer comparison.
    {
        if (_image.valid())
        {
            if (rhs._image.valid())
            {
                int result = _image->compare(*rhs._image);
                if (result!=0) return result;
            }
            else
            {
                return 1; // valid lhs._image is greater than null. 
            }
        }
        else if (rhs._image.valid()) 
        {
            return -1; // valid rhs._image is greater than null. 
        }
    }

    int result = compareTexture(rhs);
    if (result!=0) return result;

    // compare each paramter in turn against the rhs.
    COMPARE_StateAttribute_Parameter(_textureWidth)
    COMPARE_StateAttribute_Parameter(_textureHeight)
    COMPARE_StateAttribute_Parameter(_subloadCallback)

    return 0; // passed all the above comparison macro's, must be equal.
}

void Texture2D::setImage(Image* image)
{
    _image = image;
    _modifiedTag.setAllElementsTo(0);
}


void Texture2D::apply(State& state) const
{

    // get the contextID (user defined ID of 0 upwards) for the 
    // current OpenGL context.
    const unsigned int contextID = state.getContextID();

    // get the globj for the current contextID.
    GLuint& handle = getTextureObject(contextID);

    if (handle != 0)
    {
        glBindTexture( GL_TEXTURE_2D, handle );
        if (getTextureParameterDirty(state.getContextID()))
            applyTexParameters(GL_TEXTURE_2D,state);

        if (_subloadCallback.valid())
        {
            _subloadCallback->subload(*this,state);
        }
        else if (_image.valid() && getModifiedTag(contextID) != _image->getModifiedTag())
        {
            applyTexImage2D_subload(GL_TEXTURE_2D,_image.get(),state,
                                    _textureWidth, _textureHeight, _numMimpmapLevels);
 
            // update the modified tag to show that it is upto date.
            getModifiedTag(contextID) = _image->getModifiedTag();
     
        }

    }
    else if (_subloadCallback.valid())
    {

        glGenTextures( 1L, (GLuint *)&handle );
        glBindTexture( GL_TEXTURE_2D, handle );

        applyTexParameters(GL_TEXTURE_2D,state);

        _subloadCallback->load(*this,state);

        // in theory the following line is redundent, but in practice
        // have found that the first frame drawn doesn't apply the textures
        // unless a second bind is called?!!
        // perhaps it is the first glBind which is not required...
        glBindTexture( GL_TEXTURE_2D, handle );

    }
    else if (_image.valid() && _image->data())
    {

        glGenTextures( 1L, (GLuint *)&handle );
        glBindTexture( GL_TEXTURE_2D, handle );

        applyTexParameters(GL_TEXTURE_2D,state);

        applyTexImage2D_load(GL_TEXTURE_2D,_image.get(),state,
                             _textureWidth, _textureHeight, _numMimpmapLevels);

        // update the modified tag to show that it is upto date.
        getModifiedTag(contextID) = _image->getModifiedTag();


        if (_unrefImageDataAfterApply)
        {
            // only unref image once all the graphics contexts has been set up.
            int numLeftToBind=0;
            for(int i=0;i<DisplaySettings::instance()->getMaxNumberOfGraphicsContexts();++i)
            {
                if (_handleList[i]==0) ++numLeftToBind;
            }
            if (numLeftToBind==0)
            {
                Texture2D* non_const_this = const_cast<Texture2D*>(this);
                non_const_this->_image = 0;
            }
        }

        // in theory the following line is redundent, but in practice
        // have found that the first frame drawn doesn't apply the textures
        // unless a second bind is called?!!
        // perhaps it is the first glBind which is not required...
        glBindTexture( GL_TEXTURE_2D, handle );
        
    }
    else
    {
        glBindTexture( GL_TEXTURE_2D, 0 );
    }
}

void Texture2D::computeInternalFormat() const
{
    if (_image.valid()) computeInternalFormatWithImage(*_image); 
}


void Texture2D::copyTexImage2D(State& state, int x, int y, int width, int height )
{
    const unsigned int contextID = state.getContextID();

    // get the globj for the current contextID.
    GLuint& handle = getTextureObject(contextID);
    
    if (handle)
    {
        if (width==(int)_textureWidth && height==(int)_textureHeight)
        {
            // we have a valid texture object which is the right size
            // so lets play clever and use copyTexSubImage2D instead.
            // this allows use to reuse the texture object and avoid
            // expensive memory allocations.
            copyTexSubImage2D(state,0 ,0, x, y, width, height);
            return;
        }
        // the relevent texture object is not of the right size so
        // needs to been deleted    
        // remove previously bound textures. 
        dirtyTextureObject();
        // note, dirtyTextureObject() dirties all the texture objects for
        // this texture, is this right?  Perhaps we should dirty just the
        // one for this context.  Note sure yet will leave till later.
        // RO July 2001.
    }
    
    
    // remove any previously assigned images as these are nolonger valid.
    _image = NULL;

    // switch off mip-mapping.
    _min_filter = LINEAR;
    _mag_filter = LINEAR;

    // Get a new 2d texture handle.
    glGenTextures( 1, &handle );

    glBindTexture( GL_TEXTURE_2D, handle );
    applyTexParameters(GL_TEXTURE_2D,state);
    glCopyTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, x, y, width, height, 0 );

    _textureWidth = width;
    _textureHeight = height;

    // inform state that this texture is the current one bound.
    state.haveAppliedAttribute(this);
}

void Texture2D::copyTexSubImage2D(State& state, int xoffset, int yoffset, int x, int y, int width, int height )
{
    const unsigned int contextID = state.getContextID();

    // get the globj for the current contextID.
    GLuint& handle = getTextureObject(contextID);
    
    if (handle)
    {

        // we have a valid image
        glBindTexture( GL_TEXTURE_2D, handle );
        applyTexParameters(GL_TEXTURE_2D,state);
        glCopyTexSubImage2D( GL_TEXTURE_2D, 0, xoffset,yoffset, x, y, width, height);

        /* Redundant, delete later */
        glBindTexture( GL_TEXTURE_2D, handle );

        // inform state that this texture is the current one bound.
        state.haveAppliedAttribute(this);

    }
    else
    {
        // no texture object already exsits for this context so need to
        // create it upfront - simply call copyTexImage2D.
        copyTexImage2D(state,x,y,width,height);
    }
}
